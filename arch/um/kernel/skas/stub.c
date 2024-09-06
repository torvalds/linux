// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
 */

#include <sysdep/stub.h>

static __always_inline int syscall_handler(struct stub_data *d)
{
	int i;
	unsigned long res;

	for (i = 0; i < d->syscall_data_len; i++) {
		struct stub_syscall *sc = &d->syscall_data[i];

		switch (sc->syscall) {
		case STUB_SYSCALL_MMAP:
			res = stub_syscall6(STUB_MMAP_NR,
					    sc->mem.addr, sc->mem.length,
					    sc->mem.prot,
					    MAP_SHARED | MAP_FIXED,
					    sc->mem.fd, sc->mem.offset);
			if (res != sc->mem.addr) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
		case STUB_SYSCALL_MUNMAP:
			res = stub_syscall2(__NR_munmap,
					    sc->mem.addr, sc->mem.length);
			if (res) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
		case STUB_SYSCALL_MPROTECT:
			res = stub_syscall3(__NR_mprotect,
					    sc->mem.addr, sc->mem.length,
					    sc->mem.prot);
			if (res) {
				d->err = res;
				d->syscall_data_len = i;
				return -1;
			}
			break;
		default:
			d->err = -95; /* EOPNOTSUPP */
			d->syscall_data_len = i;
			return -1;
		}
	}

	d->err = 0;
	d->syscall_data_len = 0;

	return 0;
}

void __section(".__syscall_stub")
stub_syscall_handler(void)
{
	struct stub_data *d = get_stub_data();

	syscall_handler(d);

	trap_myself();
}
