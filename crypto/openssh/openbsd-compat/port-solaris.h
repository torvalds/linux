/*
 * Copyright (c) 2006 Chad Mynhier.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _PORT_SOLARIS_H

#include <sys/types.h>

#include <pwd.h>

void solaris_contract_pre_fork(void);
void solaris_contract_post_fork_child(void);
void solaris_contract_post_fork_parent(pid_t pid);
void solaris_set_default_project(struct passwd *);
# ifdef USE_SOLARIS_PRIVS
#include <priv.h>
priv_set_t *solaris_basic_privset(void);
void solaris_drop_privs_pinfo_net_fork_exec(void);
void solaris_drop_privs_root_pinfo_net(void);
void solaris_drop_privs_root_pinfo_net_exec(void);
# endif /* USE_SOLARIS_PRIVS */

#endif
