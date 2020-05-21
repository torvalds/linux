/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is designed to contain the BUILD_INTERRUPT specifications for
 * all of the extra named interrupt vectors used by the architecture.
 * Usually this is the Inter Process Interrupts (IPIs)
 */

/*
 * The following vectors are part of the Linux architecture, there
 * is no hardware IRQ pin equivalent for them, they are triggered
 * through the ICC by us (IPIs)
 */
#ifdef CONFIG_SMP
BUILD_INTERRUPT(reschedule_interrupt,RESCHEDULE_VECTOR)
#endif
