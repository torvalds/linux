/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ASMARC_SEGMENT_H
#define __ASMARC_SEGMENT_H

#ifndef __ASSEMBLY__

typedef unsigned long mm_segment_t;

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define KERNEL_DS		MAKE_MM_SEG(0)
#define USER_DS			MAKE_MM_SEG(TASK_SIZE)

#define segment_eq(a, b)	((a) == (b))

#endif /* __ASSEMBLY__ */
#endif /* __ASMARC_SEGMENT_H */
