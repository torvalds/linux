/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google LLC
 */
#include <asm/kvm_host.h>
#include <asm/kvm_pkvm_module.h>

#include <nvhe/modules.h>
#include <nvhe/mm.h>
#include <nvhe/serial.h>

static void __kvm_flush_dcache_to_poc(void *addr, size_t size)
{
	kvm_flush_dcache_to_poc((unsigned long)addr, (unsigned long)size);
}

const struct pkvm_module_ops module_ops = {
	.create_private_mapping = __pkvm_create_private_mapping,
	.register_serial_driver = __pkvm_register_serial_driver,
	.puts = hyp_puts,
	.putx64 = hyp_putx64,
	.fixmap_map = hyp_fixmap_map,
	.fixmap_unmap = hyp_fixmap_unmap,
	.flush_dcache_to_poc = __kvm_flush_dcache_to_poc,
};

int __pkvm_init_module(void *module_init)
{
	int (*do_module_init)(const struct pkvm_module_ops *ops) = module_init;
	int ret;

	ret = do_module_init(&module_ops);

	return ret;
}
