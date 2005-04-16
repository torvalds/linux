
#ifndef __PPC_BOOT_CPC700_H
#define __PPC_BOOT_CPC700_H

#define CPC700_MEM_CFGADDR    0xff500008
#define CPC700_MEM_CFGDATA    0xff50000c

#define CPC700_MB0SA            0x38
#define CPC700_MB0EA            0x58
#define CPC700_MB1SA            0x3c
#define CPC700_MB1EA            0x5c
#define CPC700_MB2SA            0x40
#define CPC700_MB2EA            0x60
#define CPC700_MB3SA            0x44
#define CPC700_MB3EA            0x64
#define CPC700_MB4SA            0x48
#define CPC700_MB4EA            0x68

static inline long
cpc700_read_memreg(int reg)
{
	out_be32((volatile unsigned int *) CPC700_MEM_CFGADDR, reg);
	return in_be32((volatile unsigned int *) CPC700_MEM_CFGDATA);
}

#endif
