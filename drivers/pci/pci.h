/* Functions internal to the PCI core code */

extern int pci_hotplug (struct device *dev, char **envp, int num_envp,
			 char *buffer, int buffer_size);
extern int pci_create_sysfs_dev_files(struct pci_dev *pdev);
extern void pci_remove_sysfs_dev_files(struct pci_dev *pdev);
extern void pci_cleanup_rom(struct pci_dev *dev);
extern int pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
				  unsigned long size, unsigned long align,
				  unsigned long min, unsigned int type_mask,
				  void (*alignf)(void *, struct resource *,
					  	 unsigned long, unsigned long),
				  void *alignf_data);
/* Firmware callbacks */
extern int (*platform_pci_choose_state)(struct pci_dev *dev, pm_message_t state);
extern int (*platform_pci_set_power_state)(struct pci_dev *dev, pci_power_t state);

extern int pci_user_read_config_byte(struct pci_dev *dev, int where, u8 *val);
extern int pci_user_read_config_word(struct pci_dev *dev, int where, u16 *val);
extern int pci_user_read_config_dword(struct pci_dev *dev, int where, u32 *val);
extern int pci_user_write_config_byte(struct pci_dev *dev, int where, u8 val);
extern int pci_user_write_config_word(struct pci_dev *dev, int where, u16 val);
extern int pci_user_write_config_dword(struct pci_dev *dev, int where, u32 val);

/* PCI /proc functions */
#ifdef CONFIG_PROC_FS
extern int pci_proc_attach_device(struct pci_dev *dev);
extern int pci_proc_detach_device(struct pci_dev *dev);
extern int pci_proc_attach_bus(struct pci_bus *bus);
extern int pci_proc_detach_bus(struct pci_bus *bus);
#else
static inline int pci_proc_attach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_attach_bus(struct pci_bus *bus) { return 0; }
static inline int pci_proc_detach_bus(struct pci_bus *bus) { return 0; }
#endif

/* Functions for PCI Hotplug drivers to use */
extern unsigned int pci_do_scan_bus(struct pci_bus *bus);
extern int pci_remove_device_safe(struct pci_dev *dev);
extern unsigned char pci_max_busnr(void);
extern unsigned char pci_bus_max_busnr(struct pci_bus *bus);
extern int pci_bus_find_capability (struct pci_bus *bus, unsigned int devfn, int cap);

extern void pci_remove_legacy_files(struct pci_bus *bus);

/* Lock for read/write access to pci device and bus lists */
extern spinlock_t pci_bus_lock;

#ifdef CONFIG_X86_IO_APIC
extern int pci_msi_quirk;
#else
#define pci_msi_quirk 0
#endif

#ifdef CONFIG_PCI_MSI
void disable_msi_mode(struct pci_dev *dev, int pos, int type);
#else
static inline void disable_msi_mode(struct pci_dev *dev, int pos, int type) { }
#endif

extern int pcie_mch_quirk;
extern struct device_attribute pci_dev_attrs[];
extern struct class_device_attribute class_device_attr_cpuaffinity;

/**
 * pci_match_one_device - Tell if a PCI device structure has a matching
 *                        PCI device id structure
 * @id: single PCI device id structure to match
 * @dev: the PCI device structure to match against
 * 
 * Returns the matching pci_device_id structure or %NULL if there is no match.
 */
static inline const struct pci_device_id *
pci_match_one_device(const struct pci_device_id *id, const struct pci_dev *dev)
{
	if ((id->vendor == PCI_ANY_ID || id->vendor == dev->vendor) &&
	    (id->device == PCI_ANY_ID || id->device == dev->device) &&
	    (id->subvendor == PCI_ANY_ID || id->subvendor == dev->subsystem_vendor) &&
	    (id->subdevice == PCI_ANY_ID || id->subdevice == dev->subsystem_device) &&
	    !((id->class ^ dev->class) & id->class_mask))
		return id;
	return NULL;
}

