/*
 * Copyright (c) 1998 - 2005 Kungliga Tekniska Högskolan
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

/* $Id$ */

#ifndef __security_h__
#define __security_h__

enum protection_level {
    prot_invalid = -1,
    prot_clear = 0,
    prot_safe = 1,
    prot_confidential = 2,
    prot_private = 3
};

struct sec_client_mech {
    char *name;
    size_t size;
    int (*init)(void *);
    int (*auth)(void *, char*);
    void (*end)(void *);
    int (*check_prot)(void *, int);
    int (*overhead)(void *, int, int);
    int (*encode)(void *, void*, int, int, void**);
    int (*decode)(void *, void*, int, int);
};

struct sec_server_mech {
    char *name;
    size_t size;
    int (*init)(void *);
    void (*end)(void *);
    int (*check_prot)(void *, int);
    int (*overhead)(void *, int, int);
    int (*encode)(void *, void*, int, int, void**);
    int (*decode)(void *, void*, int, int);

    int (*auth)(void *);
    int (*adat)(void *, void*, size_t);
    size_t (*pbsz)(void *, size_t);
    int (*ccc)(void*);
    int (*userok)(void*, char*);
    int (*session)(void*, char*);
};

#define AUTH_OK		0
#define AUTH_CONTINUE	1
#define AUTH_ERROR	2

extern int ftp_do_gss_bindings;
extern int ftp_do_gss_delegate;
#ifdef FTP_SERVER
extern struct sec_server_mech krb4_server_mech, gss_server_mech;
#else
extern struct sec_client_mech krb4_client_mech, gss_client_mech;
#endif

extern int sec_complete;

#ifdef FTP_SERVER
extern char *ftp_command;
void new_ftp_command(char*);
void delete_ftp_command(void);
#endif

/* ---- */


int sec_fflush (FILE *);
int sec_fprintf (FILE *, const char *, ...)
    __attribute__ ((format (printf, 2,3)));
int sec_getc (FILE *);
int sec_putc (int, FILE *);
int sec_read (int, void *, int);
int sec_read_msg (char *, int);
int sec_vfprintf (FILE *, const char *, va_list)
    __attribute__ ((format (printf, 2,0)));
int sec_fprintf2(FILE *f, const char *fmt, ...)
    __attribute__ ((format (printf, 2,3)));
int sec_vfprintf2(FILE *, const char *, va_list)
    __attribute__ ((format (printf, 2,0)));
int sec_write (int, char *, int);

#ifdef FTP_SERVER
void adat (char *);
void auth (char *);
void ccc (void);
void mec (char *, enum protection_level);
void pbsz (int);
void prot (char *);
void delete_ftp_command (void);
void new_ftp_command (char *);
int sec_userok (char *);
int sec_session(char *);
int secure_command (void);
enum protection_level get_command_prot(void);
#else
void sec_end (void);
int sec_login (char *);
void sec_prot (int, char **);
void sec_prot_command (int, char **);
int sec_request_prot (char *);
void sec_set_protection_level (void);
void sec_status (void);

enum protection_level set_command_prot(enum protection_level);

#endif

#endif /* __security_h__ */
