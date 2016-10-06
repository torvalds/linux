OBJECT_FILES_NON_STANDARD_efi_thunk_$(BITS).o := y

obj-$(CONFIG_EFI) 		+= quirks.o efi.o efi_$(BITS).o efi_stub_$(BITS).o
obj-$(CONFIG_ACPI_BGRT) += efi-bgrt.o
obj-$(CONFIG_EARLY_PRINTK_EFI)	+= early_printk.o
obj-$(CONFIG_EFI_MIXED)		+= efi_thunk_$(BITS).o
