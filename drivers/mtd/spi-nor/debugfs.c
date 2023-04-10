// SPDX-License-Identifier: GPL-2.0

#include <linux/debugfs.h>
#include <linux/mtd/spi-nor.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

#include "core.h"

#define SPI_NOR_DEBUGFS_ROOT "spi-nor"

#define SNOR_F_NAME(name) [ilog2(SNOR_F_##name)] = #name
static const char *const snor_f_names[] = {
	SNOR_F_NAME(HAS_SR_TB),
	SNOR_F_NAME(NO_OP_CHIP_ERASE),
	SNOR_F_NAME(BROKEN_RESET),
	SNOR_F_NAME(4B_OPCODES),
	SNOR_F_NAME(HAS_4BAIT),
	SNOR_F_NAME(HAS_LOCK),
	SNOR_F_NAME(HAS_16BIT_SR),
	SNOR_F_NAME(NO_READ_CR),
	SNOR_F_NAME(HAS_SR_TB_BIT6),
	SNOR_F_NAME(HAS_4BIT_BP),
	SNOR_F_NAME(HAS_SR_BP3_BIT6),
	SNOR_F_NAME(IO_MODE_EN_VOLATILE),
	SNOR_F_NAME(SOFT_RESET),
	SNOR_F_NAME(SWP_IS_VOLATILE),
};
#undef SNOR_F_NAME

static const char *spi_nor_protocol_name(enum spi_nor_protocol proto)
{
	switch (proto) {
	case SNOR_PROTO_1_1_1:     return "1S-1S-1S";
	case SNOR_PROTO_1_1_2:     return "1S-1S-2S";
	case SNOR_PROTO_1_1_4:     return "1S-1S-4S";
	case SNOR_PROTO_1_1_8:     return "1S-1S-8S";
	case SNOR_PROTO_1_2_2:     return "1S-2S-2S";
	case SNOR_PROTO_1_4_4:     return "1S-4S-4S";
	case SNOR_PROTO_1_8_8:     return "1S-8S-8S";
	case SNOR_PROTO_2_2_2:     return "2S-2S-2S";
	case SNOR_PROTO_4_4_4:     return "4S-4S-4S";
	case SNOR_PROTO_8_8_8:     return "8S-8S-8S";
	case SNOR_PROTO_1_1_1_DTR: return "1D-1D-1D";
	case SNOR_PROTO_1_2_2_DTR: return "1D-2D-2D";
	case SNOR_PROTO_1_4_4_DTR: return "1D-4D-4D";
	case SNOR_PROTO_1_8_8_DTR: return "1D-8D-8D";
	case SNOR_PROTO_8_8_8_DTR: return "8D-8D-8D";
	}

	return "<unknown>";
}

static void spi_nor_print_flags(struct seq_file *s, unsigned long flags,
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

static int spi_nor_params_show(struct seq_file *s, void *data)
{
	struct spi_nor *nor = s->private;
	struct spi_nor_flash_parameter *params = nor->params;
	struct spi_nor_erase_map *erase_map = &params->erase_map;
	struct spi_nor_erase_region *region;
	const struct flash_info *info = nor->info;
	char buf[16], *str;
	int i;

	seq_printf(s, "name\t\t%s\n", info->name);
	seq_printf(s, "id\t\t%*ph\n", SPI_NOR_MAX_ID_LEN, nor->id);
	string_get_size(params->size, 1, STRING_UNITS_2, buf, sizeof(buf));
	seq_printf(s, "size\t\t%s\n", buf);
	seq_printf(s, "write size\t%u\n", params->writesize);
	seq_printf(s, "page size\t%u\n", params->page_size);
	seq_printf(s, "address nbytes\t%u\n", nor->addr_nbytes);

	seq_puts(s, "flags\t\t");
	spi_nor_print_flags(s, nor->flags, snor_f_names, sizeof(snor_f_names));
	seq_puts(s, "\n");

	seq_puts(s, "\nopcodes\n");
	seq_printf(s, " read\t\t0x%02x\n", nor->read_opcode);
	seq_printf(s, "  dummy cycles\t%u\n", nor->read_dummy);
	seq_printf(s, " erase\t\t0x%02x\n", nor->erase_opcode);
	seq_printf(s, " program\t0x%02x\n", nor->program_opcode);

	switch (nor->cmd_ext_type) {
	case SPI_NOR_EXT_NONE:
		str = "none";
		break;
	case SPI_NOR_EXT_REPEAT:
		str = "repeat";
		break;
	case SPI_NOR_EXT_INVERT:
		str = "invert";
		break;
	default:
		str = "<unknown>";
		break;
	}
	seq_printf(s, " 8D extension\t%s\n", str);

	seq_puts(s, "\nprotocols\n");
	seq_printf(s, " read\t\t%s\n",
		   spi_nor_protocol_name(nor->read_proto));
	seq_printf(s, " write\t\t%s\n",
		   spi_nor_protocol_name(nor->write_proto));
	seq_printf(s, " register\t%s\n",
		   spi_nor_protocol_name(nor->reg_proto));

	seq_puts(s, "\nerase commands\n");
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++) {
		struct spi_nor_erase_type *et = &erase_map->erase_type[i];

		if (et->size) {
			string_get_size(et->size, 1, STRING_UNITS_2, buf,
					sizeof(buf));
			seq_printf(s, " %02x (%s) [%d]\n", et->opcode, buf, i);
		}
	}

	if (!(nor->flags & SNOR_F_NO_OP_CHIP_ERASE)) {
		string_get_size(params->size, 1, STRING_UNITS_2, buf, sizeof(buf));
		seq_printf(s, " %02x (%s)\n", SPINOR_OP_CHIP_ERASE, buf);
	}

	seq_puts(s, "\nsector map\n");
	seq_puts(s, " region (in hex)   | erase mask | flags\n");
	seq_puts(s, " ------------------+------------+----------\n");
	for (region = erase_map->regions;
	     region;
	     region = spi_nor_region_next(region)) {
		u64 start = region->offset & ~SNOR_ERASE_FLAGS_MASK;
		u64 flags = region->offset & SNOR_ERASE_FLAGS_MASK;
		u64 end = start + region->size - 1;

		seq_printf(s, " %08llx-%08llx |     [%c%c%c%c] | %s\n",
			   start, end,
			   flags & BIT(0) ? '0' : ' ',
			   flags & BIT(1) ? '1' : ' ',
			   flags & BIT(2) ? '2' : ' ',
			   flags & BIT(3) ? '3' : ' ',
			   flags & SNOR_OVERLAID_REGION ? "overlaid" : "");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(spi_nor_params);

static void spi_nor_print_read_cmd(struct seq_file *s, u32 cap,
				   struct spi_nor_read_command *cmd)
{
	seq_printf(s, " %s%s\n", spi_nor_protocol_name(cmd->proto),
		   cap == SNOR_HWCAPS_READ_FAST ? " (fast read)" : "");
	seq_printf(s, "  opcode\t0x%02x\n", cmd->opcode);
	seq_printf(s, "  mode cycles\t%u\n", cmd->num_mode_clocks);
	seq_printf(s, "  dummy cycles\t%u\n", cmd->num_wait_states);
}

static void spi_nor_print_pp_cmd(struct seq_file *s,
				 struct spi_nor_pp_command *cmd)
{
	seq_printf(s, " %s\n", spi_nor_protocol_name(cmd->proto));
	seq_printf(s, "  opcode\t0x%02x\n", cmd->opcode);
}

static int spi_nor_capabilities_show(struct seq_file *s, void *data)
{
	struct spi_nor *nor = s->private;
	struct spi_nor_flash_parameter *params = nor->params;
	u32 hwcaps = params->hwcaps.mask;
	int i, cmd;

	seq_puts(s, "Supported read modes by the flash\n");
	for (i = 0; i < sizeof(hwcaps) * BITS_PER_BYTE; i++) {
		if (!(hwcaps & BIT(i)))
			continue;

		cmd = spi_nor_hwcaps_read2cmd(BIT(i));
		if (cmd < 0)
			continue;

		spi_nor_print_read_cmd(s, BIT(i), &params->reads[cmd]);
		hwcaps &= ~BIT(i);
	}

	seq_puts(s, "\nSupported page program modes by the flash\n");
	for (i = 0; i < sizeof(hwcaps) * BITS_PER_BYTE; i++) {
		if (!(hwcaps & BIT(i)))
			continue;

		cmd = spi_nor_hwcaps_pp2cmd(BIT(i));
		if (cmd < 0)
			continue;

		spi_nor_print_pp_cmd(s, &params->page_programs[cmd]);
		hwcaps &= ~BIT(i);
	}

	if (hwcaps)
		seq_printf(s, "\nunknown hwcaps 0x%x\n", hwcaps);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(spi_nor_capabilities);

static void spi_nor_debugfs_unregister(void *data)
{
	struct spi_nor *nor = data;

	debugfs_remove(nor->debugfs_root);
	nor->debugfs_root = NULL;
}

static struct dentry *rootdir;

void spi_nor_debugfs_register(struct spi_nor *nor)
{
	struct dentry *d;
	int ret;

	if (!rootdir)
		rootdir = debugfs_create_dir(SPI_NOR_DEBUGFS_ROOT, NULL);

	ret = devm_add_action(nor->dev, spi_nor_debugfs_unregister, nor);
	if (ret)
		return;

	d = debugfs_create_dir(dev_name(nor->dev), rootdir);
	nor->debugfs_root = d;

	debugfs_create_file("params", 0444, d, nor, &spi_nor_params_fops);
	debugfs_create_file("capabilities", 0444, d, nor,
			    &spi_nor_capabilities_fops);
}

void spi_nor_debugfs_shutdown(void)
{
	debugfs_remove(rootdir);
}
