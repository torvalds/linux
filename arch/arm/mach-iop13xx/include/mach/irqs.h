#ifndef _IOP13XX_IRQS_H_
#define _IOP13XX_IRQS_H_

#ifndef __ASSEMBLER__
#include <linux/types.h>

/* INTPND0 CP6 R0 Page 3
 */
static inline u32 read_intpnd_0(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c0, c3, 0":"=r" (val));
	return val;
}

/* INTPND1 CP6 R1 Page 3
 */
static inline u32 read_intpnd_1(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c1, c3, 0":"=r" (val));
	return val;
}

/* INTPND2 CP6 R2 Page 3
 */
static inline u32 read_intpnd_2(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c2, c3, 0":"=r" (val));
	return val;
}

/* INTPND3 CP6 R3 Page 3
 */
static inline u32 read_intpnd_3(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c3, c3, 0":"=r" (val));
	return val;
}
#endif

#define INTBASE 0
#define INTSIZE_4 1

/*
 * iop34x chipset interrupts
 */
#define IOP13XX_IRQ(x)		(IOP13XX_IRQ_OFS + (x))

/*
 * On IRQ or FIQ register
 */
#define IRQ_IOP13XX_ADMA0_EOT	(0)
#define IRQ_IOP13XX_ADMA0_EOC	(1)
#define IRQ_IOP13XX_ADMA1_EOT	(2)
#define IRQ_IOP13XX_ADMA1_EOC	(3)
#define IRQ_IOP13XX_ADMA2_EOT	(4)
#define IRQ_IOP13XX_ADMA2_EOC	(5)
#define IRQ_IOP134_WATCHDOG	(6)
#define IRQ_IOP13XX_RSVD_7	(7)
#define IRQ_IOP13XX_TIMER0	(8)
#define IRQ_IOP13XX_TIMER1	(9)
#define IRQ_IOP13XX_I2C_0	(10)
#define IRQ_IOP13XX_I2C_1	(11)
#define IRQ_IOP13XX_MSG	(12)
#define IRQ_IOP13XX_MSGIBQ	(13)
#define IRQ_IOP13XX_ATU_IM	(14)
#define IRQ_IOP13XX_ATU_BIST	(15)
#define IRQ_IOP13XX_PPMU	(16)
#define IRQ_IOP13XX_COREPMU	(17)
#define IRQ_IOP13XX_CORECACHE	(18)
#define IRQ_IOP13XX_RSVD_19	(19)
#define IRQ_IOP13XX_RSVD_20	(20)
#define IRQ_IOP13XX_RSVD_21	(21)
#define IRQ_IOP13XX_RSVD_22	(22)
#define IRQ_IOP13XX_RSVD_23	(23)
#define IRQ_IOP13XX_XINT0	(24)
#define IRQ_IOP13XX_XINT1	(25)
#define IRQ_IOP13XX_XINT2	(26)
#define IRQ_IOP13XX_XINT3	(27)
#define IRQ_IOP13XX_XINT4	(28)
#define IRQ_IOP13XX_XINT5	(29)
#define IRQ_IOP13XX_XINT6	(30)
#define IRQ_IOP13XX_XINT7	(31)
				      /* IINTSRC1 bit */
#define IRQ_IOP13XX_XINT8	(32)  /* 0  */
#define IRQ_IOP13XX_XINT9	(33)  /* 1  */
#define IRQ_IOP13XX_XINT10	(34)  /* 2  */
#define IRQ_IOP13XX_XINT11	(35)  /* 3  */
#define IRQ_IOP13XX_XINT12	(36)  /* 4  */
#define IRQ_IOP13XX_XINT13	(37)  /* 5  */
#define IRQ_IOP13XX_XINT14	(38)  /* 6  */
#define IRQ_IOP13XX_XINT15	(39)  /* 7  */
#define IRQ_IOP13XX_RSVD_40	(40)  /* 8  */
#define IRQ_IOP13XX_RSVD_41	(41)  /* 9  */
#define IRQ_IOP13XX_RSVD_42	(42)  /* 10 */
#define IRQ_IOP13XX_RSVD_43	(43)  /* 11 */
#define IRQ_IOP13XX_RSVD_44	(44)  /* 12 */
#define IRQ_IOP13XX_RSVD_45	(45)  /* 13 */
#define IRQ_IOP13XX_RSVD_46	(46)  /* 14 */
#define IRQ_IOP13XX_RSVD_47	(47)  /* 15 */
#define IRQ_IOP13XX_RSVD_48	(48)  /* 16 */
#define IRQ_IOP13XX_RSVD_49	(49)  /* 17 */
#define IRQ_IOP13XX_RSVD_50	(50)  /* 18 */
#define IRQ_IOP13XX_UART0	(51)  /* 19 */
#define IRQ_IOP13XX_UART1	(52)  /* 20 */
#define IRQ_IOP13XX_PBIE	(53)  /* 21 */
#define IRQ_IOP13XX_ATU_CRW	(54)  /* 22 */
#define IRQ_IOP13XX_ATU_ERR	(55)  /* 23 */
#define IRQ_IOP13XX_MCU_ERR	(56)  /* 24 */
#define IRQ_IOP13XX_ADMA0_ERR	(57)  /* 25 */
#define IRQ_IOP13XX_ADMA1_ERR	(58)  /* 26 */
#define IRQ_IOP13XX_ADMA2_ERR	(59)  /* 27 */
#define IRQ_IOP13XX_RSVD_60	(60)  /* 28 */
#define IRQ_IOP13XX_RSVD_61	(61)  /* 29 */
#define IRQ_IOP13XX_MSG_ERR	(62)  /* 30 */
#define IRQ_IOP13XX_RSVD_63	(63)  /* 31 */
				      /* IINTSRC2 bit */
#define IRQ_IOP13XX_INTERPROC	(64)  /* 0  */
#define IRQ_IOP13XX_RSVD_65	(65)  /* 1  */
#define IRQ_IOP13XX_RSVD_66	(66)  /* 2  */
#define IRQ_IOP13XX_RSVD_67	(67)  /* 3  */
#define IRQ_IOP13XX_RSVD_68	(68)  /* 4  */
#define IRQ_IOP13XX_RSVD_69	(69)  /* 5  */
#define IRQ_IOP13XX_RSVD_70	(70)  /* 6  */
#define IRQ_IOP13XX_RSVD_71	(71)  /* 7  */
#define IRQ_IOP13XX_RSVD_72	(72)  /* 8  */
#define IRQ_IOP13XX_RSVD_73	(73)  /* 9  */
#define IRQ_IOP13XX_RSVD_74	(74)  /* 10 */
#define IRQ_IOP13XX_RSVD_75	(75)  /* 11 */
#define IRQ_IOP13XX_RSVD_76	(76)  /* 12 */
#define IRQ_IOP13XX_RSVD_77	(77)  /* 13 */
#define IRQ_IOP13XX_RSVD_78	(78)  /* 14 */
#define IRQ_IOP13XX_RSVD_79	(79)  /* 15 */
#define IRQ_IOP13XX_RSVD_80	(80)  /* 16 */
#define IRQ_IOP13XX_RSVD_81	(81)  /* 17 */
#define IRQ_IOP13XX_RSVD_82	(82)  /* 18 */
#define IRQ_IOP13XX_RSVD_83	(83)  /* 19 */
#define IRQ_IOP13XX_RSVD_84	(84)  /* 20 */
#define IRQ_IOP13XX_RSVD_85	(85)  /* 21 */
#define IRQ_IOP13XX_RSVD_86	(86)  /* 22 */
#define IRQ_IOP13XX_RSVD_87	(87)  /* 23 */
#define IRQ_IOP13XX_RSVD_88	(88)  /* 24 */
#define IRQ_IOP13XX_RSVD_89	(89)  /* 25 */
#define IRQ_IOP13XX_RSVD_90	(90)  /* 26 */
#define IRQ_IOP13XX_RSVD_91	(91)  /* 27 */
#define IRQ_IOP13XX_RSVD_92	(92)  /* 28 */
#define IRQ_IOP13XX_RSVD_93	(93)  /* 29 */
#define IRQ_IOP13XX_SIB_ERR	(94)  /* 30 */
#define IRQ_IOP13XX_SRAM_ERR	(95)  /* 31 */
				      /* IINTSRC3 bit */
#define IRQ_IOP13XX_I2C_2	(96)  /* 0  */
#define IRQ_IOP13XX_ATUE_BIST	(97)  /* 1  */
#define IRQ_IOP13XX_ATUE_CRW	(98)  /* 2  */
#define IRQ_IOP13XX_ATUE_ERR	(99)  /* 3  */
#define IRQ_IOP13XX_IMU	(100) /* 4  */
#define IRQ_IOP13XX_RSVD_101	(101) /* 5  */
#define IRQ_IOP13XX_RSVD_102	(102) /* 6  */
#define IRQ_IOP13XX_TPMI0_OUT	(103) /* 7  */
#define IRQ_IOP13XX_TPMI1_OUT	(104) /* 8  */
#define IRQ_IOP13XX_TPMI2_OUT	(105) /* 9  */
#define IRQ_IOP13XX_TPMI3_OUT	(106) /* 10 */
#define IRQ_IOP13XX_ATUE_IMA	(107) /* 11 */
#define IRQ_IOP13XX_ATUE_IMB	(108) /* 12 */
#define IRQ_IOP13XX_ATUE_IMC	(109) /* 13 */
#define IRQ_IOP13XX_ATUE_IMD	(110) /* 14 */
#define IRQ_IOP13XX_MU_MSI_TB	(111) /* 15 */
#define IRQ_IOP13XX_RSVD_112	(112) /* 16 */
#define IRQ_IOP13XX_INBD_MSI	(113) /* 17 */
#define IRQ_IOP13XX_RSVD_114	(114) /* 18 */
#define IRQ_IOP13XX_RSVD_115	(115) /* 19 */
#define IRQ_IOP13XX_RSVD_116	(116) /* 20 */
#define IRQ_IOP13XX_RSVD_117	(117) /* 21 */
#define IRQ_IOP13XX_RSVD_118	(118) /* 22 */
#define IRQ_IOP13XX_RSVD_119	(119) /* 23 */
#define IRQ_IOP13XX_RSVD_120	(120) /* 24 */
#define IRQ_IOP13XX_RSVD_121	(121) /* 25 */
#define IRQ_IOP13XX_RSVD_122	(122) /* 26 */
#define IRQ_IOP13XX_RSVD_123	(123) /* 27 */
#define IRQ_IOP13XX_RSVD_124	(124) /* 28 */
#define IRQ_IOP13XX_RSVD_125	(125) /* 29 */
#define IRQ_IOP13XX_RSVD_126	(126) /* 30 */
#define IRQ_IOP13XX_HPI	(127) /* 31 */

#ifdef CONFIG_PCI_MSI
#define IRQ_IOP13XX_MSI_0	(IRQ_IOP13XX_HPI + 1)
#define NR_IOP13XX_IRQS 	(IRQ_IOP13XX_MSI_0 + 128)
#else
#define NR_IOP13XX_IRQS	(IRQ_IOP13XX_HPI + 1)
#endif

#endif /* _IOP13XX_IRQ_H_ */
