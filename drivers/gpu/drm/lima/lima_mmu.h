/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_MMU_H__
#define __LIMA_MMU_H__

struct lima_ip;
struct lima_vm;

int lima_mmu_init(struct lima_ip *ip);
void lima_mmu_fini(struct lima_ip *ip);

void lima_mmu_switch_vm(struct lima_ip *ip, struct lima_vm *vm);
void lima_mmu_page_fault_resume(struct lima_ip *ip);

#endif
