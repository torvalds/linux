/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#ifndef __VM_FLAGS_X86_H
#define __VM_FLAGS_X86_H

#ifdef CONFIG_X86_32

#define VMA_DATA_DEFAULT_FLAGS	VMA_DATA_FLAGS_TSK_EXEC

#else

#define VMA_STACK_DEFAULT_FLAGS append_vma_flags(VMA_DATA_FLAGS_EXEC, VMA_GROWSDOWN_BIT)

#endif
#endif
