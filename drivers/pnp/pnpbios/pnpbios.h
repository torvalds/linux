/*
 * pnpbios.h - contains local definitions
 */

#pragma pack(1)
union pnp_bios_install_struct {
	struct {
		u32 signature;    /* "$PnP" */
		u8 version;	  /* in BCD */
		u8 length;	  /* length in bytes, currently 21h */
		u16 control;	  /* system capabilities */
		u8 checksum;	  /* all bytes must add up to 0 */

		u32 eventflag;    /* phys. address of the event flag */
		u16 rmoffset;     /* real mode entry point */
		u16 rmcseg;
		u16 pm16offset;   /* 16 bit protected mode entry */
		u32 pm16cseg;
		u32 deviceID;	  /* EISA encoded system ID or 0 */
		u16 rmdseg;	  /* real mode data segment */
		u32 pm16dseg;	  /* 16 bit pm data segment base */
	} fields;
	char chars[0x21];	  /* To calculate the checksum */
};
#pragma pack()

extern int pnp_bios_present(void);
extern int  pnpbios_dont_use_current_config;

extern int pnpbios_parse_data_stream(struct pnp_dev *dev, struct pnp_bios_node * node);
extern int pnpbios_read_resources_from_node(struct pnp_resource_table *res, struct pnp_bios_node * node);
extern int pnpbios_write_resources_to_node(struct pnp_resource_table *res, struct pnp_bios_node * node);
extern void pnpid32_to_pnpid(u32 id, char *str);

extern void pnpbios_print_status(const char * module, u16 status);
extern void pnpbios_calls_init(union pnp_bios_install_struct * header);

#ifdef CONFIG_PNPBIOS_PROC_FS
extern int pnpbios_interface_attach_device(struct pnp_bios_node * node);
extern int pnpbios_proc_init (void);
extern void pnpbios_proc_exit (void);
#else
static inline int pnpbios_interface_attach_device(struct pnp_bios_node * node) { return 0; }
static inline int pnpbios_proc_init (void) { return 0; }
static inline void pnpbios_proc_exit (void) { ; }
#endif /* CONFIG_PNPBIOS_PROC_FS */
