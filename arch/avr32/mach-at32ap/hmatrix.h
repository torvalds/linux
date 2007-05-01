/*
 * Register definitions for High-Speed Bus Matrix
 */
#ifndef __HMATRIX_H
#define __HMATRIX_H

/* HMATRIX register offsets */
#define HMATRIX_MCFG0				0x0000
#define HMATRIX_MCFG1				0x0004
#define HMATRIX_MCFG2				0x0008
#define HMATRIX_MCFG3				0x000c
#define HMATRIX_MCFG4				0x0010
#define HMATRIX_MCFG5				0x0014
#define HMATRIX_MCFG6				0x0018
#define HMATRIX_MCFG7				0x001c
#define HMATRIX_MCFG8				0x0020
#define HMATRIX_MCFG9				0x0024
#define HMATRIX_MCFG10				0x0028
#define HMATRIX_MCFG11				0x002c
#define HMATRIX_MCFG12				0x0030
#define HMATRIX_MCFG13				0x0034
#define HMATRIX_MCFG14				0x0038
#define HMATRIX_MCFG15				0x003c
#define HMATRIX_SCFG0				0x0040
#define HMATRIX_SCFG1				0x0044
#define HMATRIX_SCFG2				0x0048
#define HMATRIX_SCFG3				0x004c
#define HMATRIX_SCFG4				0x0050
#define HMATRIX_SCFG5				0x0054
#define HMATRIX_SCFG6				0x0058
#define HMATRIX_SCFG7				0x005c
#define HMATRIX_SCFG8				0x0060
#define HMATRIX_SCFG9				0x0064
#define HMATRIX_SCFG10				0x0068
#define HMATRIX_SCFG11				0x006c
#define HMATRIX_SCFG12				0x0070
#define HMATRIX_SCFG13				0x0074
#define HMATRIX_SCFG14				0x0078
#define HMATRIX_SCFG15				0x007c
#define HMATRIX_PRAS0				0x0080
#define HMATRIX_PRBS0				0x0084
#define HMATRIX_PRAS1				0x0088
#define HMATRIX_PRBS1				0x008c
#define HMATRIX_PRAS2				0x0090
#define HMATRIX_PRBS2				0x0094
#define HMATRIX_PRAS3				0x0098
#define HMATRIX_PRBS3				0x009c
#define HMATRIX_PRAS4				0x00a0
#define HMATRIX_PRBS4				0x00a4
#define HMATRIX_PRAS5				0x00a8
#define HMATRIX_PRBS5				0x00ac
#define HMATRIX_PRAS6				0x00b0
#define HMATRIX_PRBS6				0x00b4
#define HMATRIX_PRAS7				0x00b8
#define HMATRIX_PRBS7				0x00bc
#define HMATRIX_PRAS8				0x00c0
#define HMATRIX_PRBS8				0x00c4
#define HMATRIX_PRAS9				0x00c8
#define HMATRIX_PRBS9				0x00cc
#define HMATRIX_PRAS10				0x00d0
#define HMATRIX_PRBS10				0x00d4
#define HMATRIX_PRAS11				0x00d8
#define HMATRIX_PRBS11				0x00dc
#define HMATRIX_PRAS12				0x00e0
#define HMATRIX_PRBS12				0x00e4
#define HMATRIX_PRAS13				0x00e8
#define HMATRIX_PRBS13				0x00ec
#define HMATRIX_PRAS14				0x00f0
#define HMATRIX_PRBS14				0x00f4
#define HMATRIX_PRAS15				0x00f8
#define HMATRIX_PRBS15				0x00fc
#define HMATRIX_MRCR				0x0100
#define HMATRIX_SFR0				0x0110
#define HMATRIX_SFR1				0x0114
#define HMATRIX_SFR2				0x0118
#define HMATRIX_SFR3				0x011c
#define HMATRIX_SFR4				0x0120
#define HMATRIX_SFR5				0x0124
#define HMATRIX_SFR6				0x0128
#define HMATRIX_SFR7				0x012c
#define HMATRIX_SFR8				0x0130
#define HMATRIX_SFR9				0x0134
#define HMATRIX_SFR10				0x0138
#define HMATRIX_SFR11				0x013c
#define HMATRIX_SFR12				0x0140
#define HMATRIX_SFR13				0x0144
#define HMATRIX_SFR14				0x0148
#define HMATRIX_SFR15				0x014c

/* Bitfields in MCFGx */
#define HMATRIX_ULBT_OFFSET			0
#define HMATRIX_ULBT_SIZE			3

/* Bitfields in SCFGx */
#define HMATRIX_SLOT_CYCLE_OFFSET		0
#define HMATRIX_SLOT_CYCLE_SIZE			8
#define HMATRIX_DEFMSTR_TYPE_OFFSET		16
#define HMATRIX_DEFMSTR_TYPE_SIZE		2
#define HMATRIX_FIXED_DEFMSTR_OFFSET		18
#define HMATRIX_FIXED_DEFMSTR_SIZE		4
#define HMATRIX_ARBT_OFFSET			24
#define HMATRIX_ARBT_SIZE			2

/* Bitfields in PRASx */
#define HMATRIX_M0PR_OFFSET			0
#define HMATRIX_M0PR_SIZE			4
#define HMATRIX_M1PR_OFFSET			4
#define HMATRIX_M1PR_SIZE			4
#define HMATRIX_M2PR_OFFSET			8
#define HMATRIX_M2PR_SIZE			4
#define HMATRIX_M3PR_OFFSET			12
#define HMATRIX_M3PR_SIZE			4
#define HMATRIX_M4PR_OFFSET			16
#define HMATRIX_M4PR_SIZE			4
#define HMATRIX_M5PR_OFFSET			20
#define HMATRIX_M5PR_SIZE			4
#define HMATRIX_M6PR_OFFSET			24
#define HMATRIX_M6PR_SIZE			4
#define HMATRIX_M7PR_OFFSET			28
#define HMATRIX_M7PR_SIZE			4

/* Bitfields in PRBSx */
#define HMATRIX_M8PR_OFFSET			0
#define HMATRIX_M8PR_SIZE			4
#define HMATRIX_M9PR_OFFSET			4
#define HMATRIX_M9PR_SIZE			4
#define HMATRIX_M10PR_OFFSET			8
#define HMATRIX_M10PR_SIZE			4
#define HMATRIX_M11PR_OFFSET			12
#define HMATRIX_M11PR_SIZE			4
#define HMATRIX_M12PR_OFFSET			16
#define HMATRIX_M12PR_SIZE			4
#define HMATRIX_M13PR_OFFSET			20
#define HMATRIX_M13PR_SIZE			4
#define HMATRIX_M14PR_OFFSET			24
#define HMATRIX_M14PR_SIZE			4
#define HMATRIX_M15PR_OFFSET			28
#define HMATRIX_M15PR_SIZE			4

/* Bitfields in SFR4 */
#define HMATRIX_CS1A_OFFSET			1
#define HMATRIX_CS1A_SIZE			1
#define HMATRIX_CS3A_OFFSET			3
#define HMATRIX_CS3A_SIZE			1
#define HMATRIX_CS4A_OFFSET			4
#define HMATRIX_CS4A_SIZE			1
#define HMATRIX_CS5A_OFFSET			5
#define HMATRIX_CS5A_SIZE			1
#define HMATRIX_DBPUC_OFFSET			8
#define HMATRIX_DBPUC_SIZE			1

/* Constants for ULBT */
#define HMATRIX_ULBT_INFINITE			0
#define HMATRIX_ULBT_SINGLE			1
#define HMATRIX_ULBT_FOUR_BEAT			2
#define HMATRIX_ULBT_EIGHT_BEAT			3
#define HMATRIX_ULBT_SIXTEEN_BEAT		4

/* Constants for DEFMSTR_TYPE */
#define HMATRIX_DEFMSTR_TYPE_NO_DEFAULT		0
#define HMATRIX_DEFMSTR_TYPE_LAST_DEFAULT	1
#define HMATRIX_DEFMSTR_TYPE_FIXED_DEFAULT	2

/* Constants for ARBT */
#define HMATRIX_ARBT_ROUND_ROBIN		0
#define HMATRIX_ARBT_FIXED_PRIORITY		1

/* Bit manipulation macros */
#define HMATRIX_BIT(name)					\
	(1 << HMATRIX_##name##_OFFSET)
#define HMATRIX_BF(name,value)					\
	(((value) & ((1 << HMATRIX_##name##_SIZE) - 1))		\
	 << HMATRIX_##name##_OFFSET)
#define HMATRIX_BFEXT(name,value)				\
	(((value) >> HMATRIX_##name##_OFFSET)			\
	 & ((1 << HMATRIX_##name##_SIZE) - 1))
#define HMATRIX_BFINS(name,value,old)				\
	(((old) & ~(((1 << HMATRIX_##name##_SIZE) - 1)		\
		    << HMATRIX_##name##_OFFSET))		\
	 | HMATRIX_BF(name,value))

#endif /* __HMATRIX_H */
