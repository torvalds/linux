#ifndef _IEEE1394_CONFIG_ROMS_H
#define _IEEE1394_CONFIG_ROMS_H

struct hpsb_host;

int hpsb_default_host_entry(struct hpsb_host *host);
int hpsb_init_config_roms(void);
void hpsb_cleanup_config_roms(void);
int hpsb_add_extra_config_roms(struct hpsb_host *host);
void hpsb_remove_extra_config_roms(struct hpsb_host *host);

/* List of flags to check if a host contains a certain extra config rom
 * entry. Available in the host->config_roms member. */
#define HPSB_CONFIG_ROM_ENTRY_IP1394		0x00000001

#endif /* _IEEE1394_CONFIG_ROMS_H */
