#ifndef _M68K_HP300HW_H
#define _M68K_HP300HW_H

extern unsigned long hp300_model;

/* This information was taken from NetBSD */
#define	HP_320		(0)	/* 16MHz 68020+HP MMU+16K external cache */
#define	HP_330		(1)	/* 16MHz 68020+68851 MMU */
#define	HP_340		(2)	/* 16MHz 68030 */
#define	HP_345		(3)	/* 50MHz 68030+32K external cache */
#define	HP_350		(4)	/* 25MHz 68020+HP MMU+32K external cache */
#define	HP_360		(5)	/* 25MHz 68030 */
#define	HP_370		(6)	/* 33MHz 68030+64K external cache */
#define	HP_375		(7)	/* 50MHz 68030+32K external cache */
#define	HP_380		(8)	/* 25MHz 68040 */
#define	HP_385		(9)	/* 33MHz 68040 */

#define	HP_400		(10)	/* 50MHz 68030+32K external cache */
#define	HP_425T		(11)	/* 25MHz 68040 - model 425t */
#define	HP_425S		(12)	/* 25MHz 68040 - model 425s */
#define HP_425E		(13)	/* 25MHz 68040 - model 425e */
#define HP_433T		(14)	/* 33MHz 68040 - model 433t */
#define HP_433S		(15)	/* 33MHz 68040 - model 433s */

#endif /* _M68K_HP300HW_H */
