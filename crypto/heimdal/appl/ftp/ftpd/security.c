/*
 * Copyright (c) 1998-2002, 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef FTP_SERVER
#include "ftpd_locl.h"
#else
#include "ftp_locl.h"
#endif

RCSID("$Id$");

static enum protection_level command_prot;
static enum protection_level data_prot;
static size_t buffer_size;

struct buffer {
    void *data;
    size_t size;
    size_t index;
    int eof_flag;
};

static struct buffer in_buffer, out_buffer;
int sec_complete;

static struct {
    enum protection_level level;
    const char *name;
} level_names[] = {
    { prot_clear, "clear" },
    { prot_safe, "safe" },
    { prot_confidential, "confidential" },
    { prot_private, "private" }
};

static const char *
level_to_name(enum protection_level level)
{
    int i;
    for(i = 0; i < sizeof(level_names) / sizeof(level_names[0]); i++)
	if(level_names[i].level == level)
	    return level_names[i].name;
    return "unknown";
}

#ifndef FTP_SERVER /* not used in server */
static enum protection_level
name_to_level(const char *name)
{
    int i;
    for(i = 0; i < sizeof(level_names) / sizeof(level_names[0]); i++)
	if(!strncasecmp(level_names[i].name, name, strlen(name)))
	    return level_names[i].level;
    return prot_invalid;
}
#endif

#ifdef FTP_SERVER

static struct sec_server_mech *mechs[] = {
#ifdef KRB5
    &gss_server_mech,
#endif
    NULL
};

static struct sec_server_mech *mech;

#else

static struct sec_client_mech *mechs[] = {
#ifdef KRB5
    &gss_client_mech,
#endif
    NULL
};

static struct sec_client_mech *mech;

#endif

static void *app_data;

int
sec_getc(FILE *F)
{
    if(sec_complete && data_prot) {
	char c;
	if(sec_read(fileno(F), &c, 1) <= 0)
	    return EOF;
	return c;
    } else
	return getc(F);
}

static int
block_read(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    int b;
    while(len) {
	b = read(fd, p, len);
	if (b == 0)
	    return 0;
	else if (b < 0)
	    return -1;
	len -= b;
	p += b;
    }
    return p - (unsigned char*)buf;
}

static int
block_write(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    int b;
    while(len) {
	b = write(fd, p, len);
	if(b < 0)
	    return -1;
	len -= b;
	p += b;
    }
    return p - (unsigned char*)buf;
}

static int
sec_get_data(int fd, struct buffer *buf, int level)
{
    int len;
    int b;
    void *tmp;

    b = block_read(fd, &len, sizeof(len));
    if (b == 0)
	return 0;
    else if (b < 0)
	return -1;
    len = ntohl(len);
    tmp = realloc(buf->data, len);
    if (tmp == NULL)
	return -1;
    buf->data = tmp;
    b = block_read(fd, buf->data, len);
    if (b == 0)
	return 0;
    else if (b < 0)
	return -1;
    buf->size = (*mech->decode)(app_data, buf->data, len, data_prot);
    buf->index = 0;
    return 0;
}

static size_t
buffer_read(struct buffer *buf, void *dataptr, size_t len)
{
    len = min(len, buf->size - buf->index);
    memcpy(dataptr, (char*)buf->data + buf->index, len);
    buf->index += len;
    return len;
}

static size_t
buffer_write(struct buffer *buf, void *dataptr, size_t len)
{
    if(buf->index + len > buf->size) {
	void *tmp;
	if(buf->data == NULL)
	    tmp = malloc(1024);
	else
	    tmp = realloc(buf->data, buf->index + len);
	if(tmp == NULL)
	    return -1;
	buf->data = tmp;
	buf->size = buf->index + len;
    }
    memcpy((char*)buf->data + buf->index, dataptr, len);
    buf->index += len;
    return len;
}

int
sec_read(int fd, void *dataptr, int length)
{
    size_t len;
    int rx = 0;

    if(sec_complete == 0 || data_prot == 0)
	return read(fd, dataptr, length);

    if(in_buffer.eof_flag){
	in_buffer.eof_flag = 0;
	return 0;
    }

    len = buffer_read(&in_buffer, dataptr, length);
    length -= len;
    rx += len;
    dataptr = (char*)dataptr + len;

    while(length){
	int ret;

	ret = sec_get_data(fd, &in_buffer, data_prot);
	if (ret < 0)
	    return -1;
	if(ret == 0 && in_buffer.size == 0) {
	    if(rx)
		in_buffer.eof_flag = 1;
	    return rx;
	}
	len = buffer_read(&in_buffer, dataptr, length);
	length -= len;
	rx += len;
	dataptr = (char*)dataptr + len;
    }
    return rx;
}

static int
sec_send(int fd, char *from, int length)
{
    int bytes;
    void *buf;
    bytes = (*mech->encode)(app_data, from, length, data_prot, &buf);
    bytes = htonl(bytes);
    block_write(fd, &bytes, sizeof(bytes));
    block_write(fd, buf, ntohl(bytes));
    free(buf);
    return length;
}

int
sec_fflush(FILE *F)
{
    if(data_prot != prot_clear) {
	if(out_buffer.index > 0){
	    sec_write(fileno(F), out_buffer.data, out_buffer.index);
	    out_buffer.index = 0;
	}
	sec_send(fileno(F), NULL, 0);
    }
    fflush(F);
    return 0;
}

int
sec_write(int fd, char *dataptr, int length)
{
    int len = buffer_size;
    int tx = 0;

    if(data_prot == prot_clear)
	return write(fd, dataptr, length);

    len -= (*mech->overhead)(app_data, data_prot, len);
    while(length){
	if(length < len)
	    len = length;
	sec_send(fd, dataptr, len);
	length -= len;
	dataptr += len;
	tx += len;
    }
    return tx;
}

int
sec_vfprintf2(FILE *f, const char *fmt, va_list ap)
{
    char *buf;
    int ret;
    if(data_prot == prot_clear)
	return vfprintf(f, fmt, ap);
    else {
	int len;
	len = vasprintf(&buf, fmt, ap);
	if (len == -1)
	    return len;
	ret = buffer_write(&out_buffer, buf, len);
	free(buf);
	return ret;
    }
}

int
sec_fprintf2(FILE *f, const char *fmt, ...)
{
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = sec_vfprintf2(f, fmt, ap);
    va_end(ap);
    return ret;
}

int
sec_putc(int c, FILE *F)
{
    char ch = c;
    if(data_prot == prot_clear)
	return putc(c, F);

    buffer_write(&out_buffer, &ch, 1);
    if(c == '\n' || out_buffer.index >= 1024 /* XXX */) {
	sec_write(fileno(F), out_buffer.data, out_buffer.index);
	out_buffer.index = 0;
    }
    return c;
}

int
sec_read_msg(char *s, int level)
{
    int len;
    char *buf;
    int return_code;

    buf = malloc(strlen(s));
    len = base64_decode(s + 4, buf); /* XXX */

    len = (*mech->decode)(app_data, buf, len, level);
    if(len < 0)
	return -1;

    buf[len] = '\0';

    if(buf[3] == '-')
	return_code = 0;
    else
	sscanf(buf, "%d", &return_code);
    if(buf[len-1] == '\n')
	buf[len-1] = '\0';
    strcpy(s, buf);
    free(buf);
    return return_code;
}

int
sec_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char *buf;
    void *enc;
    int len;
    if(!sec_complete)
	return vfprintf(f, fmt, ap);

    if (vasprintf(&buf, fmt, ap) == -1) {
	printf("Failed to allocate command.\n");
	return -1;
    }
    len = (*mech->encode)(app_data, buf, strlen(buf), command_prot, &enc);
    free(buf);
    if(len < 0) {
	printf("Failed to encode command.\n");
	return -1;
    }
    if(base64_encode(enc, len, &buf) < 0){
	free(enc);
	printf("Out of memory base64-encoding.\n");
	return -1;
    }
    free(enc);
#ifdef FTP_SERVER
    if(command_prot == prot_safe)
	fprintf(f, "631 %s\r\n", buf);
    else if(command_prot == prot_private)
	fprintf(f, "632 %s\r\n", buf);
    else if(command_prot == prot_confidential)
	fprintf(f, "633 %s\r\n", buf);
#else
    if(command_prot == prot_safe)
	fprintf(f, "MIC %s", buf);
    else if(command_prot == prot_private)
	fprintf(f, "ENC %s", buf);
    else if(command_prot == prot_confidential)
	fprintf(f, "CONF %s", buf);
#endif
    free(buf);
    return 0;
}

int
sec_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = sec_vfprintf(f, fmt, ap);
    va_end(ap);
    return ret;
}

/* end common stuff */

#ifdef FTP_SERVER

int ccc_passed;

void
auth(char *auth_name)
{
    int i;
    void *tmp;

    for(i = 0; (mech = mechs[i]) != NULL; i++){
	if(!strcasecmp(auth_name, mech->name)){
	    tmp = realloc(app_data, mech->size);
	    if (tmp == NULL) {
		reply(431, "Unable to accept %s at this time", mech->name);
		return;
	    }
	    app_data = tmp;

	    if(mech->init && (*mech->init)(app_data) != 0) {
		reply(431, "Unable to accept %s at this time", mech->name);
		return;
	    }
	    if(mech->auth) {
		(*mech->auth)(app_data);
		return;
	    }
	    if(mech->adat)
		reply(334, "Send authorization data.");
	    else
		reply(234, "Authorization complete.");
	    return;
	}
    }
    free (app_data);
    app_data = NULL;
    reply(504, "%s is unknown to me", auth_name);
}

void
adat(char *auth_data)
{
    if(mech && !sec_complete) {
	void *buf = malloc(strlen(auth_data));
	size_t len;
	len = base64_decode(auth_data, buf);
	(*mech->adat)(app_data, buf, len);
	free(buf);
    } else
	reply(503, "You must %sissue an AUTH first.", mech ? "re-" : "");
}

void pbsz(int size)
{
    size_t new = size;
    if(!sec_complete)
	reply(503, "Incomplete security data exchange.");
    if(mech->pbsz)
	new = (*mech->pbsz)(app_data, size);
    if(buffer_size != new){
	buffer_size = size;
    }
    if(new != size)
	reply(200, "PBSZ=%lu", (unsigned long)new);
    else
	reply(200, "OK");
}

void
prot(char *pl)
{
    int p = -1;

    if(buffer_size == 0){
	reply(503, "No protection buffer size negotiated.");
	return;
    }

    if(!strcasecmp(pl, "C"))
	p = prot_clear;
    else if(!strcasecmp(pl, "S"))
	p = prot_safe;
    else if(!strcasecmp(pl, "E"))
	p = prot_confidential;
    else if(!strcasecmp(pl, "P"))
	p = prot_private;
    else {
	reply(504, "Unrecognized protection level.");
	return;
    }

    if(sec_complete){
	if((*mech->check_prot)(app_data, p)){
	    reply(536, "%s does not support %s protection.",
		  mech->name, level_to_name(p));
	}else{
	    data_prot = (enum protection_level)p;
	    reply(200, "Data protection is %s.", level_to_name(p));
	}
    }else{
	reply(503, "Incomplete security data exchange.");
    }
}

void ccc(void)
{
    if(sec_complete){
	if(mech->ccc && (*mech->ccc)(app_data) == 0) {
	    command_prot = data_prot = prot_clear;
	    ccc_passed = 1;
	} else
	    reply(534, "You must be joking.");
    }else
	reply(503, "Incomplete security data exchange.");
}

void mec(char *msg, enum protection_level level)
{
    void *buf;
    size_t len, buf_size;
    if(!sec_complete) {
	reply(503, "Incomplete security data exchange.");
	return;
    }
    buf_size = strlen(msg) + 2;
    buf = malloc(buf_size);
    if (buf == NULL) {
	reply(501, "Failed to allocate %lu", (unsigned long)buf_size);
	return;
    }
    len = base64_decode(msg, buf);
    command_prot = level;
    if(len == (size_t)-1) {
	free(buf);
	reply(501, "Failed to base64-decode command");
	return;
    }
    len = (*mech->decode)(app_data, buf, len, level);
    if(len == (size_t)-1) {
	free(buf);
	reply(535, "Failed to decode command");
	return;
    }
    ((char*)buf)[len] = '\0';
    if(strstr((char*)buf, "\r\n") == NULL)
	strlcat((char*)buf, "\r\n", buf_size);
    new_ftp_command(buf);
}

/* ------------------------------------------------------------ */

int
sec_userok(char *userstr)
{
    if(sec_complete)
	return (*mech->userok)(app_data, userstr);
    return 0;
}

int
sec_session(char *user)
{
    if(sec_complete && mech->session)
	return (*mech->session)(app_data, user);
    return 0;
}

char *ftp_command;

void
new_ftp_command(char *command)
{
    ftp_command = command;
}

void
delete_ftp_command(void)
{
    free(ftp_command);
    ftp_command = NULL;
}

int
secure_command(void)
{
    return ftp_command != NULL;
}

enum protection_level
get_command_prot(void)
{
    return command_prot;
}

#else /* FTP_SERVER */

void
sec_status(void)
{
    if(sec_complete){
	printf("Using %s for authentication.\n", mech->name);
	printf("Using %s command channel.\n", level_to_name(command_prot));
	printf("Using %s data channel.\n", level_to_name(data_prot));
	if(buffer_size > 0)
	    printf("Protection buffer size: %lu.\n",
		   (unsigned long)buffer_size);
    }else{
	printf("Not using any security mechanism.\n");
    }
}

static int
sec_prot_internal(int level)
{
    int ret;
    char *p;
    unsigned int s = 1048576;

    int old_verbose = verbose;
    verbose = 0;

    if(!sec_complete){
	printf("No security data exchange has taken place.\n");
	return -1;
    }

    if(level){
	ret = command("PBSZ %u", s);
	if(ret != COMPLETE){
	    printf("Failed to set protection buffer size.\n");
	    return -1;
	}
	buffer_size = s;
	p = strstr(reply_string, "PBSZ=");
	if(p)
	    sscanf(p, "PBSZ=%u", &s);
	if(s < buffer_size)
	    buffer_size = s;
    }
    verbose = old_verbose;
    ret = command("PROT %c", level["CSEP"]); /* XXX :-) */
    if(ret != COMPLETE){
	printf("Failed to set protection level.\n");
	return -1;
    }

    data_prot = (enum protection_level)level;
    return 0;
}

enum protection_level
set_command_prot(enum protection_level level)
{
    int ret;
    enum protection_level old = command_prot;
    if(level != command_prot && level == prot_clear) {
	ret = command("CCC");
	if(ret != COMPLETE) {
	    printf("Failed to clear command channel.\n");
	    return prot_invalid;
	}
    }
    command_prot = level;
    return old;
}

void
sec_prot(int argc, char **argv)
{
    int level = -1;

    if(argc > 3)
	goto usage;

    if(argc == 1) {
	sec_status();
	return;
    }
    if(!sec_complete) {
	printf("No security data exchange has taken place.\n");
	code = -1;
	return;
    }
    level = name_to_level(argv[argc - 1]);

    if(level == -1)
	goto usage;

    if((*mech->check_prot)(app_data, level)) {
	printf("%s does not implement %s protection.\n",
	       mech->name, level_to_name(level));
	code = -1;
	return;
    }

    if(argc == 2 || strncasecmp(argv[1], "data", strlen(argv[1])) == 0) {
	if(sec_prot_internal(level) < 0){
	    code = -1;
	    return;
	}
    } else if(strncasecmp(argv[1], "command", strlen(argv[1])) == 0) {
	if(set_command_prot(level) < 0) {
	    code = -1;
	    return;
	}
    } else
	goto usage;
    code = 0;
    return;
 usage:
    printf("usage: %s [command|data] [clear|safe|confidential|private]\n",
	   argv[0]);
    code = -1;
}

void
sec_prot_command(int argc, char **argv)
{
    int level;

    if(argc > 2)
	goto usage;

    if(!sec_complete) {
	printf("No security data exchange has taken place.\n");
	code = -1;
	return;
    }

    if(argc == 1) {
	sec_status();
    } else {
	level = name_to_level(argv[1]);
	if(level == -1)
	    goto usage;

	if((*mech->check_prot)(app_data, level)) {
	    printf("%s does not implement %s protection.\n",
		   mech->name, level_to_name(level));
	    code = -1;
	    return;
	}
	if(set_command_prot(level) < 0) {
	    code = -1;
	    return;
	}
    }
    code = 0;
    return;
 usage:
    printf("usage: %s [clear|safe|confidential|private]\n",
	   argv[0]);
    code = -1;
}

static enum protection_level request_data_prot;

void
sec_set_protection_level(void)
{
    if(sec_complete && data_prot != request_data_prot)
	sec_prot_internal(request_data_prot);
}


int
sec_request_prot(char *level)
{
    int l = name_to_level(level);
    if(l == -1)
	return -1;
    request_data_prot = (enum protection_level)l;
    return 0;
}

int
sec_login(char *host)
{
    int ret;
    struct sec_client_mech **m;
    int old_verbose = verbose;

    verbose = -1; /* shut up all messages this will produce (they
		     are usually not very user friendly) */

    for(m = mechs; *m && (*m)->name; m++) {
	void *tmp;

	tmp = realloc(app_data, (*m)->size);
	if (tmp == NULL) {
	    warnx ("realloc %lu failed", (unsigned long)(*m)->size);
	    return -1;
	}
	app_data = tmp;

	if((*m)->init && (*(*m)->init)(app_data) != 0) {
	    printf("Skipping %s...\n", (*m)->name);
	    continue;
	}
	printf("Trying %s...\n", (*m)->name);
	ret = command("AUTH %s", (*m)->name);
	if(ret != CONTINUE){
	    if(code == 504){
		printf("%s is not supported by the server.\n", (*m)->name);
	    }else if(code == 534){
		printf("%s rejected as security mechanism.\n", (*m)->name);
	    }else if(ret == ERROR) {
		printf("The server doesn't support the FTP "
		       "security extensions.\n");
		verbose = old_verbose;
		return -1;
	    }
	    continue;
	}

	ret = (*(*m)->auth)(app_data, host);

	if(ret == AUTH_CONTINUE)
	    continue;
	else if(ret != AUTH_OK){
	    /* mechanism is supposed to output error string */
	    verbose = old_verbose;
	    return -1;
	}
	mech = *m;
	sec_complete = 1;
	if(doencrypt) {
	    command_prot = prot_private;
	    request_data_prot = prot_private;
	} else {
	    command_prot = prot_safe;
	}
	break;
    }

    verbose = old_verbose;
    return *m == NULL;
}

void
sec_end(void)
{
    if (mech != NULL) {
	if(mech->end)
	    (*mech->end)(app_data);
	if (app_data != NULL) {
	    memset(app_data, 0, mech->size);
	    free(app_data);
	    app_data = NULL;
	}
    }
    sec_complete = 0;
    data_prot = (enum protection_level)0;
}

#endif /* FTP_SERVER */

