# SPDX-License-Identifier: GPL-2.0
OBJECT_FILES_NON_STANDARD_efi_thunk_$(BITS).o := y
KASAN_SANITIZE := n
GCOV_PROFILE := n

obj-$(CONFIG_EFI) 		+= quirks.o efi.o efi_$(BITS).o efi_stub_$(BITS).o
obj-$(CONFIG_EFI_MIXED)		+= efi_thunk_$(BITS).o
