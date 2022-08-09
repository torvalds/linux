/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Changbin Du <changbin.du@gmail.com>
 */
#ifndef __TRACE_IRQ_H
#define __TRACE_IRQ_H

void __trace_hardirqs_on(void);
void __trace_hardirqs_off(void);

#endif /* __TRACE_IRQ_H */
