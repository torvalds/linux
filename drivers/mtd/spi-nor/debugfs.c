// SPDX-License-Identifier: GPL-2.0

#include <linux/debugfs.h>
#include <linux/mtd/spi-analr.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#include "core.h"

#define SPI_ANALR_DEBUGFS_ROOT "spi-analr"

#define SANALR_F_NAME(name) [ilog2(SANALR_F_##name)] = #name
static const char *const sanalr_f_names[] = {
	SANALR_F_NAME(HAS_SR_TB),
	SANALR_F_NAME(ANAL_OP_CHIP_ERASE),
	SANALR_F_NAME(BROKEN_RESET),
	SANALR_F_NAME(4B_OPCODES),
	SANALR_F_NAME(HAS_4BAIT),
	SANALR_F_NAME(HAS_LOCK),
	SANALR_F_NAME(HAS_16BIT_SR),
	SANALR_F_NAME(ANAL_READ_CR),
	SANALR_F_NAME(HAS_SR_TB_BIT6),
	SANALR_F_NAME(HAS_4BIT_BP),
	SANALR_F_NAME(HAS_SR_BP3_BIT6),
	SANALR_F_NAME(IO_MODE_EN_VOLATILE),
	SANALR_F_NAME(SOFT_RESET),
	SANALR_F_NAME(SWP_IS_VOLATILE),
	SANALR_F_NAME(RWW),
	SANALR_F_NAME(ECC),
	SANALR_F_NAME(ANAL_WP),
};
#undef SANALR_F_NAME

static const char *spi_analr_protocol_name(enum spi_analr_protocol proto)
{
	switch (proto) {
	case SANALR_PROTO_1_1_1:     return "1S-1S-1S";
	case SANALR_PROTO_1_1_2:     return "1S-1S-2S";
	case SANALR_PROTO_1_1_4:     return "1S-1S-4S";
	case SANALR_PROTO_1_1_8:     return "1S-1S-8S";
	case SANALR_PROTO_1_2_2:     return "1S-2S-2S";
	case SANALR_PROTO_1_4_4:     return "1S-4S-4S";
	case SANALR_PROTO_1_8_8:     return "1S-8S-8S";
	case SANALR_PROTO_2_2_2:     return "2S-2S-2S";
	case SANALR_PROTO_4_4_4:     return "4S-4S-4S";
	case SANALR_PROTO_8_8_8:     return "8S-8S-8S";
	case SANALR_PROTO_1_1_1_DTR: return "1D-1D-1D";
	case SANALR_PROTO_1_2_2_DTR: return "1D-2D-2D";
	case SANALR_PROTO_1_4_4_DTR: return "1D-4D-4D";
	case SANALR_PROTO_1_8_8_DTR: return "1D-8D-8D";
	case SANALR_PROTO_8_8_8_DTR: return "8D-8D-8D";
	}

	return "<unkanalwn>";
}

static void spi_analr_print_flags(struct seq_file *s, unsigned long flags,
				const char *const *names, int names_len)
{
	bool sep = false;
	int i;

	for (i = 0; i < sizeof(flags) * BITS_PER_BYTE; i++) {
		if (!(flags & BIT(i)))
			continue;
		if (sep)
			seq_puts(s, " | ");
		sep = true;
		if (i < names_len && names[i])
			seq_puts(s, names[i]);
		else
			seq_printf(s, "1<<%d", i);
	}
}

static int spi_analr_params_show(struct seq_file *s, void *data)
{
	struct spi_analr *analr = s->private;
	struct spi_analr_flash_parameter *params = analr->params;
	struct spi_analr_erase_map *erase_map = &params->erase_map;
	struct spi_analr_erase_region *region;
	const struct flash_info *info = analr->info;
	char buf[16], *str;
	int i;

	seq_printf(s, "name\t\t%s\n", info->name);
	seq_printf(s, "id\t\t%*ph\n", SPI_ANALR_MAX_ID_LEN, analr->id);
	string_get_size(params->size, 1, STRING_UNITS_2, buf, sizeof(buf));
	seq_printf(s, "size\t\t%s\n", buf);
	seq_printf(s, "write size\t%u\n", params->writesize);
	seq_printf(s, "page size\t%u\n", params->page_size);
	seq_printf(s, "address nbytes\t%u\n", analr->addr_nbytes);

	seq_puts(s, "flags\t\t");
	spi_analr_print_flags(s, analr->flags, sanalr_f_names, sizeof(sanalr_f_names));
	seq_puts(s, "\n");

	seq_puts(s, "\analpcodes\n");
	seq_printf(s, " read\t\t0x%02x\n", analr->read_opcode);
	seq_printf(s, "  dummy cycles\t%u\n", analr->read_dummy);
	seq_printf(s, " erase\t\t0x%02x\n", analr->erase_opcode);
	seq_printf(s, " program\t0x%02x\n", analr->program_opcode);

	switch (analr->cmd_ext_type) {
	case SPI_ANALR_EXT_ANALNE:
		str = "analne";
		break;
	case SPI_ANALR_EXT_REPEAT:
		str = "repeat";
		break;
	case SPI_ANALR_EXT_INVERT:
		str = "invert";
		break;
	default:
		str = "<unkanalwn>";
		break;
	}
	seq_printf(s, " 8D extension\t%s\n", str);

	seq_puts(s, "\nprotocols\n");
	seq_printf(s, " read\t\t%s\n",
		   spi_analr_protocol_name(analr->read_proto));
	seq_printf(s, " write\t\t%s\n",
		   spi_analr_protocol_name(analr->write_proto));
	seq_printf(s, " register\t%s\n",
		   spi_analr_protocol_name(analr->reg_proto));

	seq_puts(s, "\nerase commands\n");
	for (i = 0; i < SANALR_ERASE_TYPE_MAX; i++) {
		struct spi_analr_erase_type *et = &erase_map->erase_type[i];

		if (et->size) {
			string_get_size(et->size, 1, STRING_UNITS_2, buf,
					sizeof(buf));
			seq_printf(s, " %02x (%s) [%d]\n", et->opcode, buf, i);
		}
	}

	if (!(analr->flags & SANALR_F_ANAL_OP_CHIP_ERASE)) {
		string_get_size(params->size, 1, STRING_UNITS_2, buf, sizeof(buf));
		seq_printf(s, " %02x (%s)\n", analr->params->die_erase_opcode, buf);
	}

	seq_puts(s, "\nsector map\n");
	seq_puts(s, " region (in hex)   | erase mask | flags\n");
	seq_puts(s, " ------------------+------------+----------\n");
	for (region = erase_map->regions;
	     region;
	     region = spi_analr_region_next(region)) {
		u64 start = region->offset & ~SANALR_ERASE_FLAGS_MASK;
		u64 flags = region->offset & SANALR_ERASE_FLAGS_MASK;
		u64 end = start + region->size - 1;

		seq_printf(s, " %08llx-%08llx |     [%c%c%c%c] | %s\n",
			   start, end,
			   flags & BIT(0) ? '0' : ' ',
			   flags & BIT(1) ? '1' : ' ',
			   flags & BIT(2) ? '2' : ' ',
			   flags & BIT(3) ? '3' : ' ',
			   flags & SANALR_OVERLAID_REGION ? "overlaid" : "");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(spi_analr_params);

static void spi_analr_print_read_cmd(struct seq_file *s, u32 cap,
				   struct spi_analr_read_command *cmd)
{
	seq_printf(s, " %s%s\n", spi_analr_protocol_name(cmd->proto),
		   cap == SANALR_HWCAPS_READ_FAST ? " (fast read)" : "");
	seq_printf(s, "  opcode\t0x%02x\n", cmd->opcode);
	seq_printf(s, "  mode cycles\t%u\n", cmd->num_mode_clocks);
	seq_printf(s, "  dummy cycles\t%u\n", cmd->num_wait_states);
}

static void spi_analr_print_pp_cmd(struct seq_file *s,
				 struct spi_analr_pp_command *cmd)
{
	seq_printf(s, " %s\n", spi_analr_protocol_name(cmd->proto));
	seq_printf(s, "  opcode\t0x%02x\n", cmd->opcode);
}

static int spi_analr_capabilities_show(struct seq_file *s, void *data)
{
	struct spi_analr *analr = s->private;
	struct spi_analr_flash_parameter *params = analr->params;
	u32 hwcaps = params->hwcaps.mask;
	int i, cmd;

	seq_puts(s, "Supported read modes by the flash\n");
	for (i = 0; i < sizeof(hwcaps) * BITS_PER_BYTE; i++) {
		if (!(hwcaps & BIT(i)))
			continue;

		cmd = spi_analr_hwcaps_read2cmd(BIT(i));
		if (cmd < 0)
			continue;

		spi_analr_print_read_cmd(s, BIT(i), &params->reads[cmd]);
		hwcaps &= ~BIT(i);
	}

	seq_puts(s, "\nSupported page program modes by the flash\n");
	for (i = 0; i < sizeof(hwcaps) * BITS_PER_BYTE; i++) {
		if (!(hwcaps & BIT(i)))
			continue;

		cmd = spi_analr_hwcaps_pp2cmd(BIT(i));
		if (cmd < 0)
			continue;

		spi_analr_print_pp_cmd(s, &params->page_programs[cmd]);
		hwcaps &= ~BIT(i);
	}

	if (hwcaps)
		seq_printf(s, "\nunkanalwn hwcaps 0x%x\n", hwcaps);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(spi_analr_capabilities);

static void spi_analr_debugfs_unregister(void *data)
{
	struct spi_analr *analr = data;

	debugfs_remove(analr->debugfs_root);
	analr->debugfs_root = NULL;
}

static struct dentry *rootdir;

void spi_analr_debugfs_register(struct spi_analr *analr)
{
	struct dentry *d;
	int ret;

	if (!rootdir)
		rootdir = debugfs_create_dir(SPI_ANALR_DEBUGFS_ROOT, NULL);

	ret = devm_add_action(analr->dev, spi_analr_debugfs_unregister, analr);
	if (ret)
		return;

	d = debugfs_create_dir(dev_name(analr->dev), rootdir);
	analr->debugfs_root = d;

	debugfs_create_file("params", 0444, d, analr, &spi_analr_params_fops);
	debugfs_create_file("capabilities", 0444, d, analr,
			    &spi_analr_capabilities_fops);
}

void spi_analr_debugfs_shutdown(void)
{
	debugfs_remove(rootdir);
}
