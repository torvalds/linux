/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_EXEC_H
#define __ASM_ARC_EXEC_H

/* Align to 16b */
#define arch_align_stack(p) ((unsigned long)(p) & ~0xf)

#endif
