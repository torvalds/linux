#ifndef __NVBIOS_DCB_H__
#define __NVBIOS_DCB_H__

struct nouveau_bios;

enum dcb_output_type {
	DCB_OUTPUT_ANALOG	= 0x0,
	DCB_OUTPUT_TV		= 0x1,
	DCB_OUTPUT_TMDS		= 0x2,
	DCB_OUTPUT_LVDS		= 0x3,
	DCB_OUTPUT_DP		= 0x6,
	DCB_OUTPUT_EOL		= 0xe,
	DCB_OUTPUT_UNUSED	= 0xf,
	DCB_OUTPUT_ANY = -1,
};

struct dcb_output {
	int index;	/* may not be raw dcb index if merging has happened */
	enum dcb_output_type type;
	uint8_t i2c_index;
	uint8_t heads;
	uint8_t connector;
	uint8_t bus;
	uint8_t location;
	uint8_t or;
	bool duallink_possible;
	union {
		struct sor_conf {
			int link;
		} sorconf;
		struct {
			int maxfreq;
		} crtconf;
		struct {
			struct sor_conf sor;
			bool use_straps_for_mode;
			bool use_acpi_for_edid;
			bool use_power_scripts;
		} lvdsconf;
		struct {
			bool has_component_output;
		} tvconf;
		struct {
			struct sor_conf sor;
			int link_nr;
			int link_bw;
		} dpconf;
		struct {
			struct sor_conf sor;
			int slave_addr;
		} tmdsconf;
	};
	bool i2c_upper_default;
};

u16 dcb_table(struct nouveau_bios *, u8 *ver, u8 *hdr, u8 *ent, u8 *len);
u16 dcb_outp(struct nouveau_bios *, u8 idx, u8 *ver, u8 *len);
int dcb_outp_foreach(struct nouveau_bios *, void *data, int (*exec)
		     (struct nouveau_bios *, void *, int index, u16 entry));


/* BIT 'U'/'d' table encoder subtables have hashes matching them to
 * a particular set of encoders.
 *
 * This function returns true if a particular DCB entry matches.
 */
static inline bool
dcb_hash_match(struct dcb_output *dcb, u32 hash)
{
	if ((hash & 0x000000f0) != (dcb->location << 4))
		return false;
	if ((hash & 0x0000000f) != dcb->type)
		return false;
	if (!(hash & (dcb->or << 16)))
		return false;

	switch (dcb->type) {
	case DCB_OUTPUT_TMDS:
	case DCB_OUTPUT_LVDS:
	case DCB_OUTPUT_DP:
		if (hash & 0x00c00000) {
			if (!(hash & (dcb->sorconf.link << 22)))
				return false;
		}
	default:
		return true;
	}
}

#endif
