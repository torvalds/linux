/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PKUnity Operating System Timer (OST) Registers
 */
/*
 * Match Reg 0 OST_OSMR0
 */
#define OST_OSMR0	(PKUNITY_OST_BASE + 0x0000)
/*
 * Match Reg 1 OST_OSMR1
 */
#define OST_OSMR1	(PKUNITY_OST_BASE + 0x0004)
/*
 * Match Reg 2 OST_OSMR2
 */
#define OST_OSMR2	(PKUNITY_OST_BASE + 0x0008)
/*
 * Match Reg 3 OST_OSMR3
 */
#define OST_OSMR3	(PKUNITY_OST_BASE + 0x000C)
/*
 * Counter Reg OST_OSCR
 */
#define OST_OSCR	(PKUNITY_OST_BASE + 0x0010)
/*
 * Status Reg OST_OSSR
 */
#define OST_OSSR	(PKUNITY_OST_BASE + 0x0014)
/*
 * Watchdog Enable Reg OST_OWER
 */
#define OST_OWER	(PKUNITY_OST_BASE + 0x0018)
/*
 * Interrupt Enable Reg OST_OIER
 */
#define OST_OIER	(PKUNITY_OST_BASE + 0x001C)

/*
 * PWM Registers: IO base address: PKUNITY_OST_BASE + 0x80
 *      PWCR: Pulse Width Control Reg
 *      DCCR: Duty Cycle Control Reg
 *      PCR: Period Control Reg
 */
#define OST_PWM_PWCR	(0x00)
#define OST_PWM_DCCR	(0x04)
#define OST_PWM_PCR 	(0x08)

/*
 * Match detected 0 OST_OSSR_M0
 */
#define OST_OSSR_M0		FIELD(1, 1, 0)
/*
 * Match detected 1 OST_OSSR_M1
 */
#define OST_OSSR_M1		FIELD(1, 1, 1)
/*
 * Match detected 2 OST_OSSR_M2
 */
#define OST_OSSR_M2		FIELD(1, 1, 2)
/*
 * Match detected 3 OST_OSSR_M3
 */
#define OST_OSSR_M3		FIELD(1, 1, 3)

/*
 * Interrupt enable 0 OST_OIER_E0
 */
#define OST_OIER_E0		FIELD(1, 1, 0)
/*
 * Interrupt enable 1 OST_OIER_E1
 */
#define OST_OIER_E1		FIELD(1, 1, 1)
/*
 * Interrupt enable 2 OST_OIER_E2
 */
#define OST_OIER_E2		FIELD(1, 1, 2)
/*
 * Interrupt enable 3 OST_OIER_E3
 */
#define OST_OIER_E3		FIELD(1, 1, 3)

/*
 * Watchdog Match Enable OST_OWER_WME
 */
#define OST_OWER_WME		FIELD(1, 1, 0)

/*
 * PWM Full Duty Cycle OST_PWMDCCR_FDCYCLE
 */
#define OST_PWMDCCR_FDCYCLE	FIELD(1, 1, 10)

