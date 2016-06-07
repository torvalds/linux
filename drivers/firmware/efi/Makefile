#
# Makefile for linux kernel
#

#
# ARM64 maps efi runtime services in userspace addresses
# which don't have KASAN shadow. So dereference of these addresses
# in efi_call_virt() will cause crash if this code instrumented.
#
KASAN_SANITIZE_runtime-wrappers.o	:= n

obj-$(CONFIG_EFI)			+= efi.o vars.o reboot.o memattr.o
obj-$(CONFIG_EFI)			+= capsule.o
obj-$(CONFIG_EFI_VARS)			+= efivars.o
obj-$(CONFIG_EFI_ESRT)			+= esrt.o
obj-$(CONFIG_EFI_VARS_PSTORE)		+= efi-pstore.o
obj-$(CONFIG_UEFI_CPER)			+= cper.o
obj-$(CONFIG_EFI_RUNTIME_MAP)		+= runtime-map.o
obj-$(CONFIG_EFI_RUNTIME_WRAPPERS)	+= runtime-wrappers.o
obj-$(CONFIG_EFI_STUB)			+= libstub/
obj-$(CONFIG_EFI_FAKE_MEMMAP)		+= fake_mem.o
obj-$(CONFIG_EFI_BOOTLOADER_CONTROL)	+= efibc.o

arm-obj-$(CONFIG_EFI)			:= arm-init.o arm-runtime.o
obj-$(CONFIG_ARM)			+= $(arm-obj-y)
obj-$(CONFIG_ARM64)			+= $(arm-obj-y)
obj-$(CONFIG_EFI_CAPSULE_LOADER)	+= capsule-loader.o
