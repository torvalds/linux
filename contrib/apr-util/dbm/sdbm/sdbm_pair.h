/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SDBM_PAIR_H
#define SDBM_PAIR_H

/* Mini EMBED (pair.c) */
#define chkpage apu__sdbm_chkpage
#define delpair apu__sdbm_delpair
#define duppair apu__sdbm_duppair
#define fitpair apu__sdbm_fitpair
#define getnkey apu__sdbm_getnkey
#define getpair apu__sdbm_getpair
#define putpair apu__sdbm_putpair
#define splpage apu__sdbm_splpage

int fitpair(char *, int);
void  putpair(char *, apr_sdbm_datum_t, apr_sdbm_datum_t);
apr_sdbm_datum_t getpair(char *, apr_sdbm_datum_t);
int  delpair(char *, apr_sdbm_datum_t);
int  chkpage (char *);
apr_sdbm_datum_t getnkey(char *, int);
void splpage(char *, char *, long);
int duppair(char *, apr_sdbm_datum_t);

#endif /* SDBM_PAIR_H */

