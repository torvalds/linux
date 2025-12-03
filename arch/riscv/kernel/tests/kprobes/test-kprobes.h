/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef TEST_KPROBES_H
#define TEST_KPROBES_H

/*
 * The magic value that all the functions in the test_kprobes_functions array return. The test
 * installs kprobes into these functions, and verify that the functions still correctly return this
 * value.
 */
#define KPROBE_TEST_MAGIC          0xcafebabe
#define KPROBE_TEST_MAGIC_LOWER    0x0000babe
#define KPROBE_TEST_MAGIC_UPPER    0xcafe0000

#ifndef __ASSEMBLER__

/* array of addresses to install kprobes */
extern void *test_kprobes_addresses[];

/* array of functions that return KPROBE_TEST_MAGIC */
extern long (*test_kprobes_functions[])(void);

#endif /* __ASSEMBLER__ */

#endif /* TEST_KPROBES_H */
