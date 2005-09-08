/*
 * It's nice to have the LEDs on the GPIO pins available for debugging
 */
static void ddb5074_fixup(struct pci_dev *dev)
{
	extern struct pci_dev *pci_pmu;
	u8 t8;

	pci_pmu = dev;  /* for LEDs D2 and D3 */
	/* Program the lines for LEDs D2 and D3 to output */
	pci_read_config_byte(dev, 0x7d, &t8);
	t8 |= 0xc0;
	pci_write_config_byte(dev, 0x7d, t8);
	/* Turn LEDs D2 and D3 off */
	pci_read_config_byte(dev, 0x7e, &t8);
	t8 |= 0xc0;
	pci_write_config_byte(dev, 0x7e, t8);
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M7101,
	  ddb5074_fixup);
