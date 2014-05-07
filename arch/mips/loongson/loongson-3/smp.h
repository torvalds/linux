#ifndef __LOONGSON_SMP_H_
#define __LOONGSON_SMP_H_

/* for Loongson-3A smp support */

/* 4 groups(nodes) in maximum in numa case */
#define  SMP_CORE_GROUP0_BASE    0x900000003ff01000
#define  SMP_CORE_GROUP1_BASE    0x900010003ff01000
#define  SMP_CORE_GROUP2_BASE    0x900020003ff01000
#define  SMP_CORE_GROUP3_BASE    0x900030003ff01000

/* 4 cores in each group(node) */
#define  SMP_CORE0_OFFSET  0x000
#define  SMP_CORE1_OFFSET  0x100
#define  SMP_CORE2_OFFSET  0x200
#define  SMP_CORE3_OFFSET  0x300

/* ipi registers offsets */
#define  STATUS0  0x00
#define  EN0      0x04
#define  SET0     0x08
#define  CLEAR0   0x0c
#define  STATUS1  0x10
#define  MASK1    0x14
#define  SET1     0x18
#define  CLEAR1   0x1c
#define  BUF      0x20

#endif
