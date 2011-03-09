#ifndef __ASM_CPU_SH3_DAC_H
#define __ASM_CPU_SH3_DAC_H

/*
 * Copyright (C) 2003  Andriy Skulysh
 */


#define DADR0	0xa40000a0
#define DADR1	0xa40000a2
#define DACR	0xa40000a4
#define DACR_DAOE1	0x80
#define DACR_DAOE0	0x40
#define DACR_DAE	0x20


static __inline__ void sh_dac_enable(int channel)
{
	unsigned char v;
	v = __raw_readb(DACR);
	if(channel) v |= DACR_DAOE1;
	else v |= DACR_DAOE0;
	__raw_writeb(v,DACR);
}

static __inline__ void sh_dac_disable(int channel)
{
	unsigned char v;
	v = __raw_readb(DACR);
	if(channel) v &= ~DACR_DAOE1;
	else v &= ~DACR_DAOE0;
	__raw_writeb(v,DACR);
}

static __inline__ void sh_dac_output(u8 value, int channel)
{
	if(channel) __raw_writeb(value,DADR1);
	else __raw_writeb(value,DADR0);
}

#endif /* __ASM_CPU_SH3_DAC_H */
