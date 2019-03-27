/*-
 * Copyright (c) 2006-2007 Robert N. M. Watson
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Generate a series of BSM token samples in the requested directory.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <arpa/inet.h>

#include <bsm/audit_kevents.h>
#include <bsm/libbsm.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

static int	do_records, do_tokens;

static void
usage(void)
{

	fprintf(stderr, "generate [-rt] path\n");
	exit(EX_USAGE);
}

static int
open_file(const char *directory, const char *name)
{
	char pathname[PATH_MAX];
	int fd;

	snprintf(pathname, PATH_MAX, "%s/%s", directory, name);
	(void)unlink(pathname);
	fd = open(pathname, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
		err(EX_CANTCREAT, "open: %s", name);
	return (fd);
}

static void
write_file(int fd, void *buffer, size_t buflen, const char *filename)
{
	ssize_t len;

	len = write(fd, buffer, buflen);
	if (len < 0)
		err(EX_OSERR, "write_file: %s", filename);
	if (len < buflen)
		err(EX_OSERR, "write_file: short write: %s", filename);
}

/*
 * Write a single token to a file.
 */
static void
write_token(const char *directory, const char *filename, token_t *tok)
{
	u_char buffer[MAX_AUDIT_RECORD_SIZE];
	size_t buflen;
	int fd;

	buflen = MAX_AUDIT_RECORD_SIZE;
	if (au_close_token(tok, buffer, &buflen) < 0)
		err(EX_UNAVAILABLE, "au_close_token");
	fd = open_file(directory, filename);
	write_file(fd, buffer, buflen, filename);
	close(fd);
}

/*
 * Write a token to a file, wrapped in audit record header and trailer.
 */
static void
write_record(const char *directory, const char *filename, token_t *tok,
    short event)
{
	u_char buffer[MAX_AUDIT_RECORD_SIZE];
	size_t buflen;
	int au, fd;

	au = au_open();
	if (au < 0)
		err(EX_UNAVAILABLE, "au_open");
	if (au_write(au, tok) < 0)
		err(EX_UNAVAILABLE, "au_write");
	buflen = MAX_AUDIT_RECORD_SIZE;
	if (au_close_buffer(au, event, buffer, &buflen) < 0)
		err(EX_UNAVAILABLE, "au_close_buffer");
	fd = open_file(directory, filename);
	write_file(fd, buffer, buflen, filename);
	close(fd);
}

static struct timeval	 file_token_timeval = { 0x12345, 0x67890} ;

static void
generate_file_token(const char *directory, const char *token_filename)
{
	token_t *file_token;

	file_token = au_to_file("test", file_token_timeval);
	if (file_token == NULL)
		err(EX_UNAVAILABLE, "au_to_file");
	write_token(directory, token_filename, file_token);
}

static void
generate_file_record(const char *directory, const char *record_filename)
{
	token_t *file_token;

	file_token = au_to_file("test", file_token_timeval);
	if (file_token == NULL)
		err(EX_UNAVAILABLE, "au_to_file");
	write_record(directory, record_filename, file_token, AUE_NULL);
}

/*
 * AUT_OHEADER
 */

static int		 trailer_token_len = 0x12345678;

static void
generate_trailer_token(const char *directory, const char *token_filename)
{
	token_t *trailer_token;

	trailer_token = au_to_trailer(trailer_token_len);
	if (trailer_token == NULL)
		err(EX_UNAVAILABLE, "au_to_trailer");
	write_token(directory, token_filename, trailer_token);
}

static int		 header32_token_len = 0x12345678;
static au_event_t	 header32_e_type = AUE_OPEN;
static au_emod_t	 header32_e_mod = 0x4567;
static struct timeval	 header32_tm = { 0x12345, 0x67890 };

static void
generate_header32_token(const char *directory, const char *token_filename)
{
	token_t *header32_token;

	header32_token = au_to_header32_tm(header32_token_len,
	    header32_e_type, header32_e_mod, header32_tm);
	if (header32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_header32");
	write_token(directory, token_filename, header32_token);
}

/*
 * AUT_HEADER32_EX
 */

static char		 data_token_unit_print = AUP_STRING;
static char		 data_token_unit_type = AUR_CHAR;
static char		*data_token_data = "SomeData";
static char		 data_token_unit_count = sizeof("SomeData") + 1;

static void
generate_data_token(const char *directory, const char *token_filename)
{
	token_t *data_token;

	data_token = au_to_data(data_token_unit_print, data_token_unit_type,
	    data_token_unit_count, data_token_data);
	if (data_token == NULL)
		err(EX_UNAVAILABLE, "au_to_data");
	write_token(directory, token_filename, data_token);
}

static void
generate_data_record(const char *directory, const char *record_filename)
{
	token_t *data_token;

	data_token = au_to_data(data_token_unit_print, data_token_unit_type,
	    data_token_unit_count, data_token_data);
	if (data_token == NULL)
		err(EX_UNAVAILABLE, "au_to_data");
	write_record(directory, record_filename, data_token, AUE_NULL);
}

static char		 ipc_type = AT_IPC_MSG;
static int		 ipc_id = 0x12345678;

static void
generate_ipc_token(const char *directory, const char *token_filename)
{
	token_t *ipc_token;

	ipc_token = au_to_ipc(ipc_type, ipc_id);
	if (ipc_token == NULL)
		err(EX_UNAVAILABLE, "au_to_ipc");
	write_token(directory, token_filename, ipc_token);
}

static void
generate_ipc_record(const char *directory, const char *record_filename)
{
	token_t *ipc_token;

	ipc_token = au_to_ipc(ipc_type, ipc_id);
	if (ipc_token == NULL)
		err(EX_UNAVAILABLE, "au_to_ipc");
	write_record(directory, record_filename, ipc_token, AUE_NULL);
}

static char		*path_token_path = "/test/this/is/a/test";

static void
generate_path_token(const char *directory, const char *token_filename)
{
	token_t *path_token;

	path_token = au_to_path(path_token_path);
	if (path_token == NULL)
		err(EX_UNAVAILABLE, "au_to_path");
	write_token(directory, token_filename, path_token);
}

static void
generate_path_record(const char *directory, const char *record_filename)
{
	token_t *path_token;

	path_token = au_to_path(path_token_path);
	if (path_token == NULL)
		err(EX_UNAVAILABLE, "au_to_path");
	write_record(directory, record_filename, path_token, AUE_NULL);
}

static au_id_t		 subject32_auid = 0x12345678;
static uid_t		 subject32_euid = 0x01234567;
static gid_t		 subject32_egid = 0x23456789;
static uid_t		 subject32_ruid = 0x98765432;
static gid_t		 subject32_rgid = 0x09876543;
static pid_t		 subject32_pid = 0x13243546;
static au_asid_t	 subject32_sid = 0x97867564;
static au_tid_t		 subject32_tid = { 0x16593746 };
static au_tid_addr_t	 subject32_tid_addr = { 0x16593746 };

static void
generate_subject32_token(const char *directory, const char *token_filename)
{
	token_t *subject32_token;

	subject32_tid.machine = inet_addr("127.0.0.1");

	subject32_token = au_to_subject32(subject32_auid, subject32_euid,
	    subject32_egid, subject32_ruid, subject32_rgid, subject32_pid,
	    subject32_sid, &subject32_tid);
	if (subject32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_subject32");
	write_token(directory, token_filename, subject32_token);
}

static void
generate_subject32_record(const char *directory, const char *record_filename)
{
	token_t *subject32_token;

	subject32_tid.machine = inet_addr("127.0.0.1");

	subject32_token = au_to_subject32(subject32_auid, subject32_euid,
	    subject32_egid, subject32_ruid, subject32_rgid, subject32_pid,
	    subject32_sid, &subject32_tid);
	if (subject32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_subject32");
	write_record(directory, record_filename, subject32_token, AUE_NULL);
}

static void
generate_subject32ex_token(const char *directory, const char *token_filename,
    u_int32_t type)
{
	token_t *subject32ex_token;
	char *buf;

	buf = (char *)malloc(strlen(token_filename) + 6);
	if (type == AU_IPv6) {
		inet_pton(AF_INET6, "fe80::1", subject32_tid_addr.at_addr);
		subject32_tid_addr.at_type = AU_IPv6;
		sprintf(buf, "%s%s", token_filename, "-IPv6");
	} else {
		subject32_tid_addr.at_addr[0] = inet_addr("127.0.0.1");
		subject32_tid_addr.at_type = AU_IPv4;
		sprintf(buf, "%s%s", token_filename, "-IPv4");
	}

	subject32ex_token = au_to_subject32_ex(subject32_auid, subject32_euid,
	    subject32_egid, subject32_ruid, subject32_rgid, subject32_pid,
	    subject32_sid, &subject32_tid_addr);
	if (subject32ex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_subject32_ex");
	write_token(directory, buf, subject32ex_token);
	free(buf);
}

static void
generate_subject32ex_record(const char *directory, const char *record_filename,
    u_int32_t type)
{
	token_t *subject32ex_token;
	char *buf;

	buf = (char *)malloc(strlen(record_filename) + 6);
	if (type == AU_IPv6) {
		inet_pton(AF_INET6, "fe80::1", subject32_tid_addr.at_addr);
		subject32_tid_addr.at_type = AU_IPv6;
		sprintf(buf, "%s%s", record_filename, "-IPv6");
	} else {
		subject32_tid_addr.at_addr[0] = inet_addr("127.0.0.1");
		subject32_tid_addr.at_type = AU_IPv4;
		sprintf(buf, "%s%s", record_filename, "-IPv4");
	}

	subject32ex_token = au_to_subject32_ex(subject32_auid, subject32_euid,
	    subject32_egid, subject32_ruid, subject32_rgid, subject32_pid,
	    subject32_sid, &subject32_tid_addr);
	if (subject32ex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_subject32_ex");
	write_record(directory, record_filename, subject32ex_token, AUE_NULL);
	free(buf);
}

static au_id_t		 process32_auid = 0x12345678;
static uid_t		 process32_euid = 0x01234567;
static gid_t		 process32_egid = 0x23456789;
static uid_t		 process32_ruid = 0x98765432;
static gid_t		 process32_rgid = 0x09876543;
static pid_t		 process32_pid = 0x13243546;
static au_asid_t	 process32_sid = 0x97867564;
static au_tid_t		 process32_tid = { 0x16593746 };
static au_tid_addr_t	 process32_tid_addr = { 0x16593746 };

static void
generate_process32_token(const char *directory, const char *token_filename)
{
	token_t *process32_token;

	process32_tid.machine = inet_addr("127.0.0.1");

	process32_token = au_to_process32(process32_auid, process32_euid,
	    process32_egid, process32_ruid, process32_rgid, process32_pid,
	    process32_sid, &process32_tid);
	if (process32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_process32");
	write_token(directory, token_filename, process32_token);
}

static void
generate_process32_record(const char *directory, const char *record_filename)
{
	token_t *process32_token;

	process32_tid.machine = inet_addr("127.0.0.1");

	process32_token = au_to_process32(process32_auid, process32_euid,
	    process32_egid, process32_ruid, process32_rgid, process32_pid,
	    process32_sid, &process32_tid);
	if (process32_token == NULL)
		err(EX_UNAVAILABLE, "au_ti_process32");
	write_record(directory, record_filename, process32_token, AUE_NULL);
}

static void
generate_process32ex_token(const char *directory, const char *token_filename,
    u_int32_t type)
{
	token_t *process32ex_token;
	char *buf;

	buf = (char *)malloc(strlen(token_filename) + 6);
	if (type == AU_IPv6) {
		inet_pton(AF_INET6, "fe80::1", process32_tid_addr.at_addr);
		process32_tid_addr.at_type = AU_IPv6;
		sprintf(buf, "%s%s", token_filename, "-IPv6");
	} else {
		process32_tid_addr.at_addr[0] = inet_addr("127.0.0.1");
		process32_tid_addr.at_type = AU_IPv4;
		sprintf(buf, "%s%s", token_filename, "-IPv4");
	}

	process32ex_token = au_to_process32_ex(process32_auid, process32_euid,
	    process32_egid, process32_ruid, process32_rgid, process32_pid,
	    process32_sid, &process32_tid_addr);
	if (process32ex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_process32_ex");
	write_token(directory, buf, process32ex_token);
	free(buf);
}

static void
generate_process32ex_record(const char *directory, const char *record_filename,
    u_int32_t type)
{
	token_t *process32ex_token;
	char *buf;

	buf = (char *)malloc(strlen(record_filename) + 6);
	if (type == AU_IPv6) {
		inet_pton(AF_INET6, "fe80::1", process32_tid_addr.at_addr);
		process32_tid_addr.at_type = AU_IPv6;
		sprintf(buf, "%s%s", record_filename, "-IPv6");
	} else {
		process32_tid_addr.at_addr[0] = inet_addr("127.0.0.1");
		process32_tid_addr.at_type = AU_IPv4;
		sprintf(buf, "%s%s", record_filename, "-IPv4");
	}

	process32ex_token = au_to_process32_ex(process32_auid, process32_euid,
	    process32_egid, process32_ruid, process32_rgid, process32_pid,
	    process32_sid, &process32_tid_addr);
	if (process32ex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_process32_ex");
	write_record(directory, buf, process32ex_token, AUE_NULL);
	free(buf);
}

static au_id_t		 process64_auid = 0x12345678;
static uid_t		 process64_euid = 0x01234567;
static gid_t		 process64_egid = 0x23456789;
static uid_t		 process64_ruid = 0x98765432;
static gid_t		 process64_rgid = 0x09876543;
static pid_t		 process64_pid = 0x13243546;
static au_asid_t	 process64_sid = 0x97867564;
static au_tid_t		 process64_tid = { 0x16593746 };
static au_tid_addr_t	 process64_tid_addr = { 0x16593746 };

static void
generate_process64_token(const char *directory, const char *token_filename)
{
	token_t *process64_token;

	process64_tid.machine = inet_addr("127.0.0.1");

	process64_token = au_to_process64(process64_auid, process64_euid,
	    process64_egid, process64_ruid, process64_rgid, process64_pid,
	    process64_sid, &process64_tid);
	if (process64_token == NULL)
		err(EX_UNAVAILABLE, "au_to_process64");
	write_token(directory, token_filename, process64_token);
}

static void
generate_process64_record(const char *directory, const char *record_filename)
{
	token_t *process64_token;

	process64_tid.machine = inet_addr("127.0.0.1");

	process64_token = au_to_process64(process64_auid, process64_euid,
	    process64_egid, process64_ruid, process64_rgid, process64_pid,
	    process64_sid, &process64_tid);
	if (process64_token == NULL)
		err(EX_UNAVAILABLE, "au_ti_process64");
	write_record(directory, record_filename, process64_token, AUE_NULL);
}

static void
generate_process64ex_token(const char *directory, const char *token_filename,
    u_int32_t type)
{
	token_t *process64ex_token;
	char *buf;

	buf = (char *)malloc(strlen(token_filename) + 6);
	if (type == AU_IPv6) {
		inet_pton(AF_INET6, "fe80::1", process64_tid_addr.at_addr);
		process64_tid_addr.at_type = AU_IPv6;
		sprintf(buf, "%s%s", token_filename, "-IPv6");
	} else {
		process64_tid_addr.at_addr[0] = inet_addr("127.0.0.1");
		process64_tid_addr.at_type = AU_IPv4;
		sprintf(buf, "%s%s", token_filename, "-IPv4");
	}

	process64ex_token = au_to_process64_ex(process64_auid, process64_euid,
	    process64_egid, process64_ruid, process64_rgid, process64_pid,
	    process64_sid, &process64_tid_addr);
	if (process64ex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_process64_ex");
	write_token(directory, buf, process64ex_token);
	free(buf);
}

static void
generate_process64ex_record(const char *directory, const char *record_filename,
    u_int32_t type)
{
	token_t *process64ex_token;
	char *buf;

	buf = (char *)malloc(strlen(record_filename) + 6);
	if (type == AU_IPv6) {
		inet_pton(AF_INET6, "fe80::1", process64_tid_addr.at_addr);
		process64_tid_addr.at_type = AU_IPv6;
		sprintf(buf, "%s%s", record_filename, "-IPv6");
	} else {
		process64_tid_addr.at_addr[0] = inet_addr("127.0.0.1");
		process64_tid_addr.at_type = AU_IPv4;
		sprintf(buf, "%s%s", record_filename, "-IPv4");
	}

	process64ex_token = au_to_process64_ex(process64_auid, process64_euid,
	    process64_egid, process64_ruid, process64_rgid, process64_pid,
	    process64_sid, &process64_tid_addr);
	if (process64ex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_process64_ex");
	write_record(directory, buf, process64ex_token, AUE_NULL);
	free(buf);
}

static char		 return32_status = EINVAL;
static uint32_t		 return32_ret = 0x12345678;

static void
generate_return32_token(const char *directory, const char *token_filename)
{
	token_t *return32_token;

	return32_token = au_to_return32(au_errno_to_bsm(return32_status),
	    return32_ret);
	if (return32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_return32");
	write_token(directory, token_filename, return32_token);
}

static void
generate_return32_record(const char *directory, const char *record_filename)
{
	token_t *return32_token;

	return32_token = au_to_return32(au_errno_to_bsm(return32_status),
	    return32_ret);
	if (return32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_return32");
	write_record(directory, record_filename, return32_token, AUE_NULL);
}

static char		*text_token_text = "This is a test.";

static void
generate_text_token(const char *directory, const char *token_filename)
{
	token_t *text_token;

	text_token = au_to_text(text_token_text);
	if (text_token == NULL)
		err(EX_UNAVAILABLE, "au_to_text");
	write_token(directory, token_filename, text_token);
}

static void
generate_text_record(const char *directory, const char *record_filename)
{
	token_t *text_token;

	text_token = au_to_text(text_token_text);
	if (text_token == NULL)
		err(EX_UNAVAILABLE, "au_to_text");
	write_record(directory, record_filename, text_token, AUE_NULL);
}

static char		 opaque_token_data[] = {0xaa, 0xbb, 0xcc, 0xdd};
static int		 opaque_token_bytes = sizeof(opaque_token_data);

static void
generate_opaque_token(const char *directory, const char *token_filename)
{
	token_t *opaque_token;

	opaque_token = au_to_opaque(opaque_token_data, opaque_token_bytes);
	if (opaque_token == NULL)
		err(EX_UNAVAILABLE, "au_to_opaque");
	write_token(directory, token_filename, opaque_token);
}

static void
generate_opaque_record(const char *directory, const char *record_filename)
{
	token_t *opaque_token;

	opaque_token = au_to_opaque(opaque_token_data, opaque_token_bytes);
	if (opaque_token == NULL)
		err(EX_UNAVAILABLE, "au_to_opaque");
	write_record(directory, record_filename, opaque_token, AUE_NULL);
}

static struct in_addr	 in_addr_token_addr;

static void
generate_in_addr_token(const char *directory, const char *token_filename)
{
	token_t *in_addr_token;

	in_addr_token_addr.s_addr = inet_addr("192.168.100.15");

	in_addr_token = au_to_in_addr(&in_addr_token_addr);
	if (in_addr_token == NULL)
		err(EX_UNAVAILABLE, "au_to_in_addr");
	write_token(directory, token_filename, in_addr_token);
}

static void
generate_in_addr_record(const char *directory, const char *record_filename)
{
	token_t *in_addr_token;

	in_addr_token_addr.s_addr = inet_addr("192.168.100.15");

	in_addr_token = au_to_in_addr(&in_addr_token_addr);
	if (in_addr_token == NULL)
		err(EX_UNAVAILABLE, "au_to_in_addr");
	write_record(directory, record_filename, in_addr_token, AUE_NULL);
}

static struct ip	 ip_token_ip;
static u_char		 ip_token_ip_v = 4;
static uint16_t		 ip_token_ip_id = 0x5478;
static u_char		 ip_token_ip_ttl = 64;
static u_char		 ip_token_ip_p = IPPROTO_ICMP;
static struct in_addr	 ip_token_ip_src;
static struct in_addr	 ip_token_ip_dst;

static void
generate_ip_token(const char *directory, const char *token_filename)
{
	token_t *ip_token;

	ip_token_ip_src.s_addr = inet_addr("192.168.100.155");
	ip_token_ip_dst.s_addr = inet_addr("192.168.110.48");

	memset(&ip_token_ip, 0, sizeof(ip_token_ip));
	ip_token_ip.ip_v = ip_token_ip_v;
	ip_token_ip.ip_len = htons(sizeof(ip_token_ip));
	ip_token_ip.ip_id = htons(ip_token_ip_id);
	ip_token_ip.ip_ttl = ip_token_ip_ttl;
	ip_token_ip.ip_p = ip_token_ip_p;
	ip_token_ip.ip_src = ip_token_ip_src;
	ip_token_ip.ip_dst = ip_token_ip_dst;

	ip_token = au_to_ip(&ip_token_ip);
	if (ip_token == NULL)
		err(EX_UNAVAILABLE, "au_to_ip");
	write_token(directory, token_filename, ip_token);
}

static void
generate_ip_record(const char *directory, const char *record_filename)
{
	token_t *ip_token;

	ip_token_ip_src.s_addr = inet_addr("192.168.100.155");
	ip_token_ip_dst.s_addr = inet_addr("192.168.110.48");

	memset(&ip_token_ip, 0, sizeof(ip_token_ip));
	ip_token_ip.ip_v = ip_token_ip_v;
	ip_token_ip.ip_len = htons(sizeof(ip_token_ip));
	ip_token_ip.ip_id = htons(ip_token_ip_id);
	ip_token_ip.ip_ttl = ip_token_ip_ttl;
	ip_token_ip.ip_p = ip_token_ip_p;
	ip_token_ip.ip_src = ip_token_ip_src;
	ip_token_ip.ip_dst = ip_token_ip_dst;

	ip_token = au_to_ip(&ip_token_ip);
	if (ip_token == NULL)
		err(EX_UNAVAILABLE, "au_to_ip");
	write_record(directory, record_filename, ip_token, AUE_NULL);
}

static u_int16_t		 iport_token_iport;

static void
generate_iport_token(const char *directory, const char *token_filename)
{
	token_t *iport_token;

	iport_token_iport = htons(80);

	iport_token = au_to_iport(iport_token_iport);
	if (iport_token == NULL)
		err(EX_UNAVAILABLE, "au_to_iport");
	write_token(directory, token_filename, iport_token);
}

static void
generate_iport_record(const char *directory, const char *record_filename)
{
	token_t *iport_token;

	iport_token_iport = htons(80);

	iport_token = au_to_iport(iport_token_iport);
	if (iport_token == NULL)
		err(EX_UNAVAILABLE, "au_to_iport");
	write_record(directory, record_filename, iport_token, AUE_NULL);
}

static char	 arg32_token_n = 3;
static char	*arg32_token_text = "test_arg32_token";
static uint32_t	 arg32_token_v = 0xabcdef00;

static void
generate_arg32_token(const char *directory, const char *token_filename)
{
	token_t *arg32_token;

	arg32_token = au_to_arg32(arg32_token_n, arg32_token_text,
	    arg32_token_v);
	if (arg32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_arg32");
	write_token(directory, token_filename, arg32_token);
}

static void
generate_arg32_record(const char *directory, const char *record_filename)
{
	token_t *arg32_token;

	arg32_token = au_to_arg32(arg32_token_n, arg32_token_text,
	    arg32_token_v);
	if (arg32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_arg32");
	write_record(directory, record_filename, arg32_token, AUE_NULL);
}

static long	 seq_audit_count = 0x12345678;

static void
generate_seq_token(const char *directory, const char *token_filename)
{
	token_t *seq_token;

	seq_token = au_to_seq(seq_audit_count);
	if (seq_token == NULL)
		err(EX_UNAVAILABLE, "au_to_seq");
	write_token(directory, token_filename, seq_token);
}

static void
generate_seq_record(const char *directory, const char *record_filename)
{
	token_t *seq_token;

	seq_token = au_to_seq(seq_audit_count);
	if (seq_token == NULL)
		err(EX_UNAVAILABLE, "au_to_seq");
	write_record(directory, record_filename, seq_token, AUE_NULL);
}

#if 0
/*
 * AUT_ACL
 */

static void
generate_attr_token(const char *directory, const char *token_filename)
{
	token_t *attr_token;

}

static void
generate_attr_record(const char *directory, const char *record_filename)
{
	token_t *attr_token;

}

static void
generate_ipc_perm_token(const char *directory, const char *token_filename)
{
	token_t *ipc_perm_token;

}

static void
generate_ipc_perm_record(const char *directory, const char *record_filename)
{
	token_t *ipc_perm_token;

}
#endif

#if 0
/*
 * AUT_LABEL
 */

static void
generate_groups_token(const char *directory, const char *token_filename)
{
	token_t *groups_token;

}

static void
generate_groups_record(const char *directory, const char *record_filename)
{
	token_t *groups_token;

}
#endif

/*
 * AUT_ILABEL
 */

/*
 * AUT_SLABEL
 */

/*
 * AUT_CLEAR
 */

/*
 * AUT_PRIV
 */

/*
 * AUT_UPRIV
 */

/*
 * AUT_LIAISON
 */

/*
 * AUT_NEWGROUPS
 */

/*
 * AUT_EXEC_ARGS
 */

/*
 * AUT_EXEC_ENV
 */

#if 0
static void
generate_attr32_token(const char *directory, const char *token_filename)
{
	token_t *attr32_token;

}

static void
generate_attr32_record(const char *directory, const char *record_filename)
{
	token_t *attr32_token;

}
#endif

static char	*zonename_sample = "testzone";

static void
generate_zonename_token(const char *directory, const char *token_filename)
{
	token_t *zonename_token;

	zonename_token = au_to_zonename(zonename_sample);
	if (zonename_token == NULL)
		err(EX_UNAVAILABLE, "au_to_zonename");
	write_token(directory, token_filename, zonename_token);
}

static void
generate_zonename_record(const char *directory, const char *record_filename)
{
	token_t *zonename_token;

	zonename_token = au_to_zonename(zonename_sample);
	if (zonename_token == NULL)
		err(EX_UNAVAILABLE, "au_to_zonename");
	write_record(directory, record_filename, zonename_token, AUE_NULL);
}

static u_short socketex_domain = PF_INET;
static u_short socketex_type = SOCK_STREAM;
static struct sockaddr_in socketex_laddr, socketex_raddr;

static void
generate_socketex_token(const char *directory, const char *token_filename)
{
	token_t *socketex_token;

	bzero(&socketex_laddr, sizeof(socketex_laddr));
	socketex_laddr.sin_family = AF_INET;
	socketex_laddr.sin_len = sizeof(socketex_laddr);
	socketex_laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	bzero(&socketex_raddr, sizeof(socketex_raddr));
	socketex_raddr.sin_family = AF_INET;
	socketex_raddr.sin_len = sizeof(socketex_raddr);
	socketex_raddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	socketex_token = au_to_socket_ex(au_domain_to_bsm(socketex_domain),
	    au_socket_type_to_bsm(socketex_type),
	    (struct sockaddr *)&socketex_laddr,
	    (struct sockaddr *)&socketex_raddr);
	if (socketex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_socket_ex");
	write_token(directory, token_filename, socketex_token);
}

static void
generate_socketex_record(const char *directory, const char *record_filename)
{
	token_t *socketex_token;

	bzero(&socketex_laddr, sizeof(socketex_laddr));
	socketex_laddr.sin_family = AF_INET;
	socketex_laddr.sin_len = sizeof(socketex_laddr);
	socketex_laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	bzero(&socketex_raddr, sizeof(socketex_raddr));
	socketex_raddr.sin_family = AF_INET;
	socketex_raddr.sin_len = sizeof(socketex_raddr);
	socketex_raddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	socketex_token = au_to_socket_ex(au_domain_to_bsm(socketex_domain),
	    au_socket_type_to_bsm(socketex_type),
	    (struct sockaddr *)&socketex_laddr,
	    (struct sockaddr *)&socketex_raddr);
	if (socketex_token == NULL)
		err(EX_UNAVAILABLE, "au_to_socket_ex");
	write_record(directory, record_filename, socketex_token, AUE_NULL);
}

/*
 * Generate a series of error-number specific return tokens in records.
 */
static void
generate_error_record(const char *directory, const char *filename, int error)
{
	char pathname[PATH_MAX];
	token_t *return32_token;

	return32_token = au_to_return32(au_errno_to_bsm(error), -1);
	if (return32_token == NULL)
		err(EX_UNAVAILABLE, "au_to_return32");
	(void)snprintf(pathname, PATH_MAX, "%s_record", filename);
	write_record(directory, pathname, return32_token, AUE_NULL);
}

/*
 * Not all the error numbers, just a few present on all platforms for now.
 */
const struct {
	int error_number;
	const char *error_name;
} error_list[] = {
	{ EPERM, "EPERM" },
	{ ENOENT, "ENOENT" },
	{ ESRCH, "ESRCH" },
	{ EINTR, "EINTR" },
	{ EIO, "EIO" },
	{ ENXIO, "ENXIO" },
	{ E2BIG, "E2BIG" },
	{ ENOEXEC, "ENOEXEC" },
	{ EBADF, "EBADF" },
	{ ECHILD, "ECHILD" },
	{ EDEADLK, "EDEADLK" },
	{ ENOMEM, "ENOMEM" },
	{ EACCES, "EACCES" },
	{ EFAULT, "EFAULT" },
	{ ENOTBLK, "ENOTBLK" },
	{ EBUSY, "EBUSY" },
	{ EEXIST, "EEXIST" },
	{ EXDEV, "EXDEV" },
	{ ENODEV, "ENODEV" },
	{ ENOTDIR, "ENOTDIR" },
	{ EISDIR, "EISDIR" },
	{ EINVAL, "EINVAL" },
	{ ENFILE, "ENFILE" },
	{ EMFILE, "EMFILE" },
	{ ENOTTY, "ENOTTY" },
	{ ETXTBSY, "ETXTBSY" },
	{ EFBIG, "EFBIG" },
	{ ENOSPC, "ENOSPC" },
	{ ESPIPE, "ESPIPE" },
	{ EROFS, "EROFS" },
	{ EMLINK, "EMLINK" },
	{ EPIPE, "EPIPE" }
};
const int error_list_count = sizeof(error_list)/sizeof(error_list[0]);

static void
do_error_records(const char *directory)
{
	int i;

	for (i = 0; i < error_list_count; i++)
		generate_error_record(directory, error_list[i].error_name,
		    error_list[i].error_number);
}

int
main(int argc, char *argv[])
{
	const char *directory;
	int ch;

	while ((ch = getopt(argc, argv, "rt")) != -1) {
		switch (ch) {
		case 'r':
			do_records++;
			break;

		case 't':
			do_tokens++;
			break;

		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	directory = argv[0];

	if (mkdir(directory, 0755) < 0 && errno != EEXIST)
		err(EX_OSERR, "mkdir: %s", directory);

	if (do_tokens) {
		generate_file_token(directory, "file_token");
		generate_trailer_token(directory, "trailer_token");
		generate_header32_token(directory, "header32_token");
		generate_data_token(directory, "data_token");
		generate_ipc_token(directory, "ipc_token");
		generate_path_token(directory, "path_token");
		generate_subject32_token(directory, "subject32_token");
		generate_subject32ex_token(directory, "subject32ex_token",
		    AU_IPv4);
		generate_subject32ex_token(directory, "subject32ex_token",
		    AU_IPv6);
		generate_process32_token(directory, "process32_token");
		generate_process32ex_token(directory, "process32ex_token",
		    AU_IPv4);
		generate_process32ex_token(directory, "process32ex_token",
		    AU_IPv6);
		generate_process64_token(directory, "process64_token");
		generate_process64ex_token(directory, "process64ex_token",
		    AU_IPv4);
		generate_process64ex_token(directory, "process64ex_token",
		    AU_IPv6);
		generate_return32_token(directory, "return32_token");
		generate_text_token(directory, "text_token");
		generate_opaque_token(directory, "opaque_token");
		generate_in_addr_token(directory, "in_addr_token");
		generate_ip_token(directory, "ip_token");
		generate_iport_token(directory, "iport_token");
		generate_arg32_token(directory, "arg32_token");
		generate_seq_token(directory, "seq_token");
#if 0
		generate_attr_token(directory,  "attr_token");
		generate_ipc_perm_token(directory, "ipc_perm_token");
		generate_groups_token(directory, "groups_token");
		generate_attr32_token(directory, "attr32_token");
#endif
		generate_zonename_token(directory, "zonename_token");
		generate_socketex_token(directory, "socketex_token");
	}

	if (do_records) {
		generate_file_record(directory, "file_record");
		generate_data_record(directory, "data_record");
		generate_ipc_record(directory, "ipc_record");
		generate_path_record(directory, "path_record");
		generate_subject32_record(directory, "subject32_record");
		generate_subject32ex_record(directory, "subject32ex_record",
		    AU_IPv4);
		generate_subject32ex_record(directory, "subject32ex_record",
		    AU_IPv6);
		generate_process32_record(directory, "process32_record");
		generate_process32ex_record(directory, "process32ex_record",
		    AU_IPv4);
		generate_process32ex_record(directory, "process32ex_record",
		    AU_IPv6);
		generate_process64_record(directory, "process64_record");
		generate_process64ex_record(directory, "process64ex_record",
		    AU_IPv4);
		generate_process64ex_record(directory, "process64ex_record",
		    AU_IPv6);
		generate_return32_record(directory, "return32_record");
		generate_text_record(directory, "text_record");
		generate_opaque_record(directory, "opaque_record");
		generate_in_addr_record(directory, "in_addr_record");
		generate_ip_record(directory, "ip_record");
		generate_iport_record(directory, "iport_record");
		generate_arg32_record(directory, "arg32_record");
		generate_seq_record(directory, "seq_record");
#if 0
		generate_attr_record(directory,  "attr_record");
		generate_ipc_perm_record(directory, "ipc_perm_record");
		generate_groups_record(directory, "groups_record");
		generate_attr32_record(directory, "attr32_record");
#endif
		generate_zonename_record(directory, "zonename_record");
		generate_socketex_record(directory, "socketex_record");
		do_error_records(directory);
	}

	return (0);
}
