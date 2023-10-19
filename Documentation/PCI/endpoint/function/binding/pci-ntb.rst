.. SPDX-License-Identifier: GPL-2.0

==========================
PCI NTB Endpoint Function
==========================

1) Create a subdirectory to pci_epf_ntb directory in configfs.

Standard EPF Configurable Fields:

================   ===========================================================
vendorid	   should be 0x104c
deviceid	   should be 0xb00d for TI's J721E SoC
revid		   don't care
progif_code	   don't care
subclass_code	   should be 0x00
baseclass_code	   should be 0x5
cache_line_size	   don't care
subsys_vendor_id   don't care
subsys_id	   don't care
interrupt_pin	   don't care
msi_interrupts	   don't care
msix_interrupts	   don't care
================   ===========================================================

2) Create a subdirectory to directory created in 1

NTB EPF specific configurable fields:

================   ===========================================================
db_count	   Number of doorbells; default = 4
mw1     	   size of memory window1
mw2     	   size of memory window2
mw3     	   size of memory window3
mw4     	   size of memory window4
num_mws     	   Number of memory windows; max = 4
spad_count     	   Number of scratchpad registers; default = 64
================   ===========================================================
