# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux kernel
#

#
# ARM64 maps efi runtime services in userspace addresses
# which don't have KASAN shadow. So dereference of these addresses
# in efi_call_virt() will cause crash if this code instrumented.
#
KASAN_SANITIZE_runtime-wrappers.o	:= n

obj-$(CONFIG_ACPI_BGRT) 		+= efi-bgrt.o
obj-$(CONFIG_EFI)			+= efi.o vars.o reboot.o memattr.o
obj-$(CONFIG_EFI)			+= capsule.o memmap.o
obj-$(CONFIG_EFI_VARS)			+= efivars.o
obj-$(CONFIG_EFI_ESRT)			+= esrt.o
obj-$(CONFIG_EFI_VARS_PSTORE)		+= efi-pstore.o
obj-$(CONFIG_UEFI_CPER)			+= cper.o
obj-$(CONFIG_EFI_RUNTIME_MAP)		+= runtime-map.o
obj-$(CONFIG_EFI_RUNTIME_WRAPPERS)	+= runtime-wrappers.o
obj-$(CONFIG_EFI_STUB)			+= libstub/
obj-$(CONFIG_EFI_FAKE_MEMMAP)		+= fake_mem.o
obj-$(CONFIG_EFI_BOOTLOADER_CONTROL)	+= efibc.o
obj-$(CONFIG_EFI_TEST)			+= test/
obj-$(CONFIG_EFI_DEV_PATH_PARSER)	+= dev-path-parser.o
obj-$(CONFIG_APPLE_PROPERTIES)		+= apple-properties.o

arm-obj-$(CONFIG_EFI)			:= arm-init.o arm-runtime.o
obj-$(CONFIG_ARM)			+= $(arm-obj-y)
obj-$(CONFIG_ARM64)			+= $(arm-obj-y)
obj-$(CONFIG_EFI_CAPSULE_LOADER)	+= capsule-loader.o
obj-$(CONFIG_UEFI_CPER_ARM)		+= cper-arm.o
