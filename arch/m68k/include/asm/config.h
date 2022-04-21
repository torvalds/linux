/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file contains prototypes provided by each m68k machine
 * to parse bootinfo data structures and to configure the machine
 */

#ifndef _M68K_CONFIG_H
#define _M68K_CONFIG_H

extern int amiga_parse_bootinfo(const struct bi_record *record);
extern int apollo_parse_bootinfo(const struct bi_record *record);
extern int atari_parse_bootinfo(const struct bi_record *record);
extern int bvme6000_parse_bootinfo(const struct bi_record *record);
extern int hp300_parse_bootinfo(const struct bi_record *record);
extern int mac_parse_bootinfo(const struct bi_record *record);
extern int mvme147_parse_bootinfo(const struct bi_record *record);
extern int mvme16x_parse_bootinfo(const struct bi_record *record);
extern int q40_parse_bootinfo(const struct bi_record *record);

extern void config_amiga(void);
extern void config_apollo(void);
extern void config_atari(void);
extern void config_bvme6000(void);
extern void config_hp300(void);
extern void config_mac(void);
extern void config_mvme147(void);
extern void config_mvme16x(void);
extern void config_q40(void);
extern void config_sun3(void);
extern void config_sun3x(void);

#endif /* _M68K_CONFIG_H */
