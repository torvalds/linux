/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#include "krb5_locl.h"

KRB5_LIB_VARIABLE const char *krb5_config_file =
#ifdef __APPLE__
"~/Library/Preferences/com.apple.Kerberos.plist" PATH_SEP
"/Library/Preferences/com.apple.Kerberos.plist" PATH_SEP
"~/Library/Preferences/edu.mit.Kerberos" PATH_SEP
"/Library/Preferences/edu.mit.Kerberos" PATH_SEP
#endif	/* __APPLE__ */
"~/.krb5/config" PATH_SEP
SYSCONFDIR "/krb5.conf"
#ifdef _WIN32
PATH_SEP "%{COMMON_APPDATA}/Kerberos/krb5.conf"
PATH_SEP "%{WINDOWS}/krb5.ini"
#else
PATH_SEP "/etc/krb5.conf"
#endif
;

KRB5_LIB_VARIABLE const char *krb5_defkeyname = KEYTAB_DEFAULT;

KRB5_LIB_VARIABLE const char *krb5_cc_type_api = "API";
KRB5_LIB_VARIABLE const char *krb5_cc_type_file = "FILE";
KRB5_LIB_VARIABLE const char *krb5_cc_type_memory = "MEMORY";
KRB5_LIB_VARIABLE const char *krb5_cc_type_kcm = "KCM";
KRB5_LIB_VARIABLE const char *krb5_cc_type_scc = "SCC";
