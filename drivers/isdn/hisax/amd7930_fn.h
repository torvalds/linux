/* 2001/10/02
 *
 * gerdes_amd7930.h     Header-file included by
 *                      gerdes_amd7930.c
 *
 * Author               Christoph Ersfeld <info@formula-n.de>
 *                      Formula-n Europe AG (www.formula-n.com)
 *                      previously Gerdes AG
 *
 *
 *                      This file is (c) under GNU PUBLIC LICENSE
 */




#define BYTE							unsigned char
#define WORD							unsigned int
#define rByteAMD(cs, reg)					cs->readisac(cs, reg)
#define wByteAMD(cs, reg, val)					cs->writeisac(cs, reg, val)
#define rWordAMD(cs, reg)					ReadWordAmd7930(cs, reg)
#define wWordAMD(cs, reg, val)					WriteWordAmd7930(cs, reg, val)
#define HIBYTE(w)						((unsigned char)((w & 0xff00) / 256))
#define LOBYTE(w)						((unsigned char)(w & 0x00ff))

#define AmdIrqOff(cs)						cs->dc.amd7930.setIrqMask(cs, 0)
#define AmdIrqOn(cs)						cs->dc.amd7930.setIrqMask(cs, 1)

#define AMD_CR		0x00
#define AMD_DR		0x01


#define DBUSY_TIMER_VALUE 80

extern void Amd7930_interrupt(struct IsdnCardState *, unsigned char);
extern void Amd7930_init(struct IsdnCardState *);
extern void setup_Amd7930(struct IsdnCardState *);
