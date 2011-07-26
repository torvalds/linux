/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#ifndef __VM_FLAGS_X86_64_H
#define __VM_FLAGS_X86_64_H

#define VM_DATA_DEFAULT_FLAGS (VM_READ | VM_WRITE | VM_EXEC | \
	VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)
#define VM_STACK_DEFAULT_FLAGS (VM_GROWSDOWN | VM_READ | VM_WRITE | \
	VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif
