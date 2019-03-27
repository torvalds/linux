/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/fsinfo/fsi_dict.c
 *
 */

/*
 * Dictionary support
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <fsi_data.h>
#include <fsinfo.h>


static int
dict_hash(char *k)
{
  u_int h;

  for (h = 0; *k; h += *k++) ;
  return h % DICTHASH;
}


dict *
new_dict(void)
{
  dict *dp = CALLOC(struct dict);

  return dp;
}


static void
dict_add_data(dict_ent *de, char *v)
{
  dict_data *dd = CALLOC(struct dict_data);

  dd->dd_data = v;
  ins_que(&dd->dd_q, de->de_q.q_back);
  de->de_count++;
}


static dict_ent *
new_dict_ent(char *k)
{
  dict_ent *de = CALLOC(struct dict_ent);

  de->de_key = k;
  init_que(&de->de_q);
  return de;
}


dict_ent *
dict_locate(dict *dp, char *k)
{
  dict_ent *de = dp->de[dict_hash(k)];

  while (de && !STREQ(de->de_key, k))
    de = de->de_next;
  return de;
}


void
dict_add(dict *dp, char *k, char *v)
{
  dict_ent *de = dict_locate(dp, k);

  if (!de) {
    dict_ent **dep = &dp->de[dict_hash(k)];
    de = new_dict_ent(k);
    de->de_next = *dep;
    *dep = de;
  }
  dict_add_data(de, v);
}


int
dict_iter(dict *dp, int (*fn) (qelem *))
{
  int i;
  int errors = 0;

  for (i = 0; i < DICTHASH; i++) {
    dict_ent *de = dp->de[i];
    while (de) {
      errors += (*fn) (&de->de_q);
      de = de->de_next;
    }
  }
  return errors;
}
