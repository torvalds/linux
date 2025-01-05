
KASAN_SANITIZE_kasan.o := n

obj-y = bootmem.o
obj-$(CONFIG_KASAN) += kasan.o
obj-$(CONFIG_MMU) += mmu_mem.o
obj-$(CONFIG_LKL_MMU_KUNIT_TEST) += mmu_test.o