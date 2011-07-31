
#ifndef _PC873xx_H_
#define _PC873xx_H_

/*
 * Control Register Values
 */
#define REG_FER	0x00
#define REG_FAR	0x01
#define REG_PTR	0x02
#define REG_FCR	0x03
#define REG_PCR	0x04
#define REG_KRR	0x05
#define REG_PMC	0x06
#define REG_TUP	0x07
#define REG_SID	0x08
#define REG_ASC	0x09
#define REG_IRC	0x0e

/*
 * Model numbers
 */
#define PC87303	0
#define PC87306	1
#define PC87312	2
#define PC87332	3
#define PC87334	4

int pc873xx_probe(void);
unsigned int pc873xx_get_base(void);
char *pc873xx_get_model(void);
void pc873xx_enable_epp19(void);
void pc873xx_enable_ide(void);

#endif
