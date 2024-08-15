/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_HALTPOLL_H
#define _ARCH_HALTPOLL_H

void arch_haltpoll_enable(unsigned int cpu);
void arch_haltpoll_disable(unsigned int cpu);

#endif
