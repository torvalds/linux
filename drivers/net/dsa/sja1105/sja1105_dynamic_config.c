// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include "sja1105.h"

/* In the dynamic configuration interface, the switch exposes a register-like
 * view of some of the static configuration tables.
 * Many times the field organization of the dynamic tables is abbreviated (not
 * all fields are dynamically reconfigurable) and different from the static
 * ones, but the key reason for having it is that we can spare a switch reset
 * for settings that can be changed dynamically.
 *
 * This file creates a per-switch-family abstraction called
 * struct sja1105_dynamic_table_ops and two operations that work with it:
 * - sja1105_dynamic_config_write
 * - sja1105_dynamic_config_read
 *
 * Compared to the struct sja1105_table_ops from sja1105_static_config.c,
 * the dynamic accessors work with a compound buffer:
 *
 * packed_buf
 *
 * |
 * V
 * +-----------------------------------------+------------------+
 * |              ENTRY BUFFER               |  COMMAND BUFFER  |
 * +-----------------------------------------+------------------+
 *
 * <----------------------- packed_size ------------------------>
 *
 * The ENTRY BUFFER may or may not have the same layout, or size, as its static
 * configuration table entry counterpart. When it does, the same packing
 * function is reused (bar exceptional cases - see
 * sja1105pqrs_dyn_l2_lookup_entry_packing).
 *
 * The reason for the COMMAND BUFFER being at the end is to be able to send
 * a dynamic write command through a single SPI burst. By the time the switch
 * reacts to the command, the ENTRY BUFFER is already populated with the data
 * sent by the core.
 *
 * The COMMAND BUFFER is always SJA1105_SIZE_DYN_CMD bytes (one 32-bit word) in
 * size.
 *
 * Sometimes the ENTRY BUFFER does not really exist (when the number of fields
 * that can be reconfigured is small), then the switch repurposes some of the
 * unused 32 bits of the COMMAND BUFFER to hold ENTRY data.
 *
 * The key members of struct sja1105_dynamic_table_ops are:
 * - .entry_packing: A function that deals with packing an ENTRY structure
 *		     into an SPI buffer, or retrieving an ENTRY structure
 *		     from one.
 *		     The @packed_buf pointer it's given does always point to
 *		     the ENTRY portion of the buffer.
 * - .cmd_packing: A function that deals with packing/unpacking the COMMAND
 *		   structure to/from the SPI buffer.
 *		   It is given the same @packed_buf pointer as .entry_packing,
 *		   so most of the time, the @packed_buf points *behind* the
 *		   COMMAND offset inside the buffer.
 *		   To access the COMMAND portion of the buffer, the function
 *		   knows its correct offset.
 *		   Giving both functions the same pointer is handy because in
 *		   extreme cases (see sja1105pqrs_dyn_l2_lookup_entry_packing)
 *		   the .entry_packing is able to jump to the COMMAND portion,
 *		   or vice-versa (sja1105pqrs_l2_lookup_cmd_packing).
 * - .access: A bitmap of:
 *	OP_READ: Set if the hardware manual marks the ENTRY portion of the
 *		 dynamic configuration table buffer as R (readable) after
 *		 an SPI read command (the switch will populate the buffer).
 *	OP_WRITE: Set if the manual marks the ENTRY portion of the dynamic
 *		  table buffer as W (writable) after an SPI write command
 *		  (the switch will read the fields provided in the buffer).
 *	OP_DEL: Set if the manual says the VALIDENT bit is supported in the
 *		COMMAND portion of this dynamic config buffer (i.e. the
 *		specified entry can be invalidated through a SPI write
 *		command).
 *	OP_SEARCH: Set if the manual says that the index of an entry can
 *		   be retrieved in the COMMAND portion of the buffer based
 *		   on its ENTRY portion, as a result of a SPI write command.
 *		   Only the TCAM-based FDB table on SJA1105 P/Q/R/S supports
 *		   this.
 * - .max_entry_count: The number of entries, counting from zero, that can be
 *		       reconfigured through the dynamic interface. If a static
 *		       table can be reconfigured at all dynamically, this
 *		       number always matches the maximum number of supported
 *		       static entries.
 * - .packed_size: The length in bytes of the compound ENTRY + COMMAND BUFFER.
 *		   Note that sometimes the compound buffer may contain holes in
 *		   it (see sja1105_vlan_lookup_cmd_packing). The @packed_buf is
 *		   contiguous however, so @packed_size includes any unused
 *		   bytes.
 * - .addr: The base SPI address at which the buffer must be written to the
 *	    switch's memory. When looking at the hardware manual, this must
 *	    always match the lowest documented address for the ENTRY, and not
 *	    that of the COMMAND, since the other 32-bit words will follow along
 *	    at the correct addresses.
 */

#define SJA1105_SIZE_DYN_CMD					4

#define SJA1105ET_SIZE_VL_LOOKUP_DYN_CMD			\
	SJA1105_SIZE_DYN_CMD

#define SJA1105PQRS_SIZE_VL_LOOKUP_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105_SIZE_VL_LOOKUP_ENTRY)

#define SJA1105ET_SIZE_MAC_CONFIG_DYN_ENTRY			\
	SJA1105_SIZE_DYN_CMD

#define SJA1105ET_SIZE_L2_LOOKUP_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105ET_SIZE_L2_LOOKUP_ENTRY)

#define SJA1105PQRS_SIZE_L2_LOOKUP_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY)

#define SJA1105_SIZE_VLAN_LOOKUP_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + 4 + SJA1105_SIZE_VLAN_LOOKUP_ENTRY)

#define SJA1105_SIZE_L2_FORWARDING_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105_SIZE_L2_FORWARDING_ENTRY)

#define SJA1105ET_SIZE_MAC_CONFIG_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105ET_SIZE_MAC_CONFIG_DYN_ENTRY)

#define SJA1105PQRS_SIZE_MAC_CONFIG_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY)

#define SJA1105ET_SIZE_L2_LOOKUP_PARAMS_DYN_CMD			\
	SJA1105_SIZE_DYN_CMD

#define SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_DYN_CMD		\
	(SJA1105_SIZE_DYN_CMD + SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY)

#define SJA1105ET_SIZE_GENERAL_PARAMS_DYN_CMD			\
	SJA1105_SIZE_DYN_CMD

#define SJA1105PQRS_SIZE_GENERAL_PARAMS_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY)

#define SJA1105PQRS_SIZE_AVB_PARAMS_DYN_CMD			\
	(SJA1105_SIZE_DYN_CMD + SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY)

#define SJA1105_SIZE_RETAGGING_DYN_CMD				\
	(SJA1105_SIZE_DYN_CMD + SJA1105_SIZE_RETAGGING_ENTRY)

#define SJA1105ET_SIZE_CBS_DYN_CMD				\
	(SJA1105_SIZE_DYN_CMD + SJA1105ET_SIZE_CBS_ENTRY)

#define SJA1105PQRS_SIZE_CBS_DYN_CMD				\
	(SJA1105_SIZE_DYN_CMD + SJA1105PQRS_SIZE_CBS_ENTRY)

#define SJA1105_MAX_DYN_CMD_SIZE				\
	SJA1105PQRS_SIZE_GENERAL_PARAMS_DYN_CMD

struct sja1105_dyn_cmd {
	bool search;
	u64 valid;
	u64 rdwrset;
	u64 errors;
	u64 valident;
	u64 index;
};

enum sja1105_hostcmd {
	SJA1105_HOSTCMD_SEARCH = 1,
	SJA1105_HOSTCMD_READ = 2,
	SJA1105_HOSTCMD_WRITE = 3,
	SJA1105_HOSTCMD_INVALIDATE = 4,
};

static void
sja1105_vl_lookup_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
			      enum packing_op op)
{
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(buf, &cmd->valid,   31, 31, size, op);
	sja1105_packing(buf, &cmd->errors,  30, 30, size, op);
	sja1105_packing(buf, &cmd->rdwrset, 29, 29, size, op);
	sja1105_packing(buf, &cmd->index,    9,  0, size, op);
}

static size_t sja1105et_vl_lookup_entry_packing(void *buf, void *entry_ptr,
						enum packing_op op)
{
	struct sja1105_vl_lookup_entry *entry = entry_ptr;
	const int size = SJA1105ET_SIZE_VL_LOOKUP_DYN_CMD;

	sja1105_packing(buf, &entry->egrmirr,  21, 17, size, op);
	sja1105_packing(buf, &entry->ingrmirr, 16, 16, size, op);
	return size;
}

static void
sja1105pqrs_l2_lookup_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				  enum packing_op op)
{
	u8 *p = buf + SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;
	u64 hostcmd;

	sja1105_packing(p, &cmd->valid,    31, 31, size, op);
	sja1105_packing(p, &cmd->rdwrset,  30, 30, size, op);
	sja1105_packing(p, &cmd->errors,   29, 29, size, op);
	sja1105_packing(p, &cmd->valident, 27, 27, size, op);

	/* VALIDENT is supposed to indicate "keep or not", but in SJA1105 E/T,
	 * using it to delete a management route was unsupported. UM10944
	 * said about it:
	 *
	 *   In case of a write access with the MGMTROUTE flag set,
	 *   the flag will be ignored. It will always be found cleared
	 *   for read accesses with the MGMTROUTE flag set.
	 *
	 * SJA1105 P/Q/R/S keeps the same behavior w.r.t. VALIDENT, but there
	 * is now another flag called HOSTCMD which does more stuff (quoting
	 * from UM11040):
	 *
	 *   A write request is accepted only when HOSTCMD is set to write host
	 *   or invalid. A read request is accepted only when HOSTCMD is set to
	 *   search host or read host.
	 *
	 * So it is possible to translate a RDWRSET/VALIDENT combination into
	 * HOSTCMD so that we keep the dynamic command API in place, and
	 * at the same time achieve compatibility with the management route
	 * command structure.
	 */
	if (cmd->rdwrset == SPI_READ) {
		if (cmd->search)
			hostcmd = SJA1105_HOSTCMD_SEARCH;
		else
			hostcmd = SJA1105_HOSTCMD_READ;
	} else {
		/* SPI_WRITE */
		if (cmd->valident)
			hostcmd = SJA1105_HOSTCMD_WRITE;
		else
			hostcmd = SJA1105_HOSTCMD_INVALIDATE;
	}
	sja1105_packing(p, &hostcmd, 25, 23, size, op);

	/* Hack - The hardware takes the 'index' field within
	 * struct sja1105_l2_lookup_entry as the index on which this command
	 * will operate. However it will ignore everything else, so 'index'
	 * is logically part of command but physically part of entry.
	 * Populate the 'index' entry field from within the command callback,
	 * such that our API doesn't need to ask for a full-blown entry
	 * structure when e.g. a delete is requested.
	 */
	sja1105_packing(buf, &cmd->index, 15, 6,
			SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY, op);
}

/* The switch is so retarded that it makes our command/entry abstraction
 * crumble apart.
 *
 * On P/Q/R/S, the switch tries to say whether a FDB entry
 * is statically programmed or dynamically learned via a flag called LOCKEDS.
 * The hardware manual says about this fiels:
 *
 *   On write will specify the format of ENTRY.
 *   On read the flag will be found cleared at times the VALID flag is found
 *   set.  The flag will also be found cleared in response to a read having the
 *   MGMTROUTE flag set.  In response to a read with the MGMTROUTE flag
 *   cleared, the flag be set if the most recent access operated on an entry
 *   that was either loaded by configuration or through dynamic reconfiguration
 *   (as opposed to automatically learned entries).
 *
 * The trouble with this flag is that it's part of the *command* to access the
 * dynamic interface, and not part of the *entry* retrieved from it.
 * Otherwise said, for a sja1105_dynamic_config_read, LOCKEDS is supposed to be
 * an output from the switch into the command buffer, and for a
 * sja1105_dynamic_config_write, the switch treats LOCKEDS as an input
 * (hence we can write either static, or automatically learned entries, from
 * the core).
 * But the manual contradicts itself in the last phrase where it says that on
 * read, LOCKEDS will be set to 1 for all FDB entries written through the
 * dynamic interface (therefore, the value of LOCKEDS from the
 * sja1105_dynamic_config_write is not really used for anything, it'll store a
 * 1 anyway).
 * This means you can't really write a FDB entry with LOCKEDS=0 (automatically
 * learned) into the switch, which kind of makes sense.
 * As for reading through the dynamic interface, it doesn't make too much sense
 * to put LOCKEDS into the command, since the switch will inevitably have to
 * ignore it (otherwise a command would be like "read the FDB entry 123, but
 * only if it's dynamically learned" <- well how am I supposed to know?) and
 * just use it as an output buffer for its findings. But guess what... that's
 * what the entry buffer is for!
 * Unfortunately, what really breaks this abstraction is the fact that it
 * wasn't designed having the fact in mind that the switch can output
 * entry-related data as writeback through the command buffer.
 * However, whether a FDB entry is statically or dynamically learned *is* part
 * of the entry and not the command data, no matter what the switch thinks.
 * In order to do that, we'll need to wrap around the
 * sja1105pqrs_l2_lookup_entry_packing from sja1105_static_config.c, and take
 * a peek outside of the caller-supplied @buf (the entry buffer), to reach the
 * command buffer.
 */
static size_t
sja1105pqrs_dyn_l2_lookup_entry_packing(void *buf, void *entry_ptr,
					enum packing_op op)
{
	struct sja1105_l2_lookup_entry *entry = entry_ptr;
	u8 *cmd = buf + SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(cmd, &entry->lockeds, 28, 28, size, op);

	return sja1105pqrs_l2_lookup_entry_packing(buf, entry_ptr, op);
}

static void
sja1105et_l2_lookup_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				enum packing_op op)
{
	u8 *p = buf + SJA1105ET_SIZE_L2_LOOKUP_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,    31, 31, size, op);
	sja1105_packing(p, &cmd->rdwrset,  30, 30, size, op);
	sja1105_packing(p, &cmd->errors,   29, 29, size, op);
	sja1105_packing(p, &cmd->valident, 27, 27, size, op);
	/* Hack - see comments above. */
	sja1105_packing(buf, &cmd->index, 29, 20,
			SJA1105ET_SIZE_L2_LOOKUP_ENTRY, op);
}

static size_t sja1105et_dyn_l2_lookup_entry_packing(void *buf, void *entry_ptr,
						    enum packing_op op)
{
	struct sja1105_l2_lookup_entry *entry = entry_ptr;
	u8 *cmd = buf + SJA1105ET_SIZE_L2_LOOKUP_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(cmd, &entry->lockeds, 28, 28, size, op);

	return sja1105et_l2_lookup_entry_packing(buf, entry_ptr, op);
}

static void
sja1105et_mgmt_route_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				 enum packing_op op)
{
	u8 *p = buf + SJA1105ET_SIZE_L2_LOOKUP_ENTRY;
	u64 mgmtroute = 1;

	sja1105et_l2_lookup_cmd_packing(buf, cmd, op);
	if (op == PACK)
		sja1105_pack(p, &mgmtroute, 26, 26, SJA1105_SIZE_DYN_CMD);
}

static size_t sja1105et_mgmt_route_entry_packing(void *buf, void *entry_ptr,
						 enum packing_op op)
{
	struct sja1105_mgmt_entry *entry = entry_ptr;
	const size_t size = SJA1105ET_SIZE_L2_LOOKUP_ENTRY;

	/* UM10944: To specify if a PTP egress timestamp shall be captured on
	 * each port upon transmission of the frame, the LSB of VLANID in the
	 * ENTRY field provided by the host must be set.
	 * Bit 1 of VLANID then specifies the register where the timestamp for
	 * this port is stored in.
	 */
	sja1105_packing(buf, &entry->tsreg,     85, 85, size, op);
	sja1105_packing(buf, &entry->takets,    84, 84, size, op);
	sja1105_packing(buf, &entry->macaddr,   83, 36, size, op);
	sja1105_packing(buf, &entry->destports, 35, 31, size, op);
	sja1105_packing(buf, &entry->enfport,   30, 30, size, op);
	return size;
}

static void
sja1105pqrs_mgmt_route_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				   enum packing_op op)
{
	u8 *p = buf + SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY;
	u64 mgmtroute = 1;

	sja1105pqrs_l2_lookup_cmd_packing(buf, cmd, op);
	if (op == PACK)
		sja1105_pack(p, &mgmtroute, 26, 26, SJA1105_SIZE_DYN_CMD);
}

static size_t sja1105pqrs_mgmt_route_entry_packing(void *buf, void *entry_ptr,
						   enum packing_op op)
{
	const size_t size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY;
	struct sja1105_mgmt_entry *entry = entry_ptr;

	/* In P/Q/R/S, enfport got renamed to mgmtvalid, but its purpose
	 * is the same (driver uses it to confirm that frame was sent).
	 * So just keep the name from E/T.
	 */
	sja1105_packing(buf, &entry->tsreg,     71, 71, size, op);
	sja1105_packing(buf, &entry->takets,    70, 70, size, op);
	sja1105_packing(buf, &entry->macaddr,   69, 22, size, op);
	sja1105_packing(buf, &entry->destports, 21, 17, size, op);
	sja1105_packing(buf, &entry->enfport,   16, 16, size, op);
	return size;
}

/* In E/T, entry is at addresses 0x27-0x28. There is a 4 byte gap at 0x29,
 * and command is at 0x2a. Similarly in P/Q/R/S there is a 1 register gap
 * between entry (0x2d, 0x2e) and command (0x30).
 */
static void
sja1105_vlan_lookup_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				enum packing_op op)
{
	u8 *p = buf + SJA1105_SIZE_VLAN_LOOKUP_ENTRY + 4;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,    31, 31, size, op);
	sja1105_packing(p, &cmd->rdwrset,  30, 30, size, op);
	sja1105_packing(p, &cmd->valident, 27, 27, size, op);
	/* Hack - see comments above, applied for 'vlanid' field of
	 * struct sja1105_vlan_lookup_entry.
	 */
	sja1105_packing(buf, &cmd->index, 38, 27,
			SJA1105_SIZE_VLAN_LOOKUP_ENTRY, op);
}

static void
sja1105_l2_forwarding_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				  enum packing_op op)
{
	u8 *p = buf + SJA1105_SIZE_L2_FORWARDING_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,   31, 31, size, op);
	sja1105_packing(p, &cmd->errors,  30, 30, size, op);
	sja1105_packing(p, &cmd->rdwrset, 29, 29, size, op);
	sja1105_packing(p, &cmd->index,    4,  0, size, op);
}

static void
sja1105et_mac_config_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				 enum packing_op op)
{
	const int size = SJA1105_SIZE_DYN_CMD;
	/* Yup, user manual definitions are reversed */
	u8 *reg1 = buf + 4;

	sja1105_packing(reg1, &cmd->valid, 31, 31, size, op);
	sja1105_packing(reg1, &cmd->index, 26, 24, size, op);
}

static size_t sja1105et_mac_config_entry_packing(void *buf, void *entry_ptr,
						 enum packing_op op)
{
	const int size = SJA1105ET_SIZE_MAC_CONFIG_DYN_ENTRY;
	struct sja1105_mac_config_entry *entry = entry_ptr;
	/* Yup, user manual definitions are reversed */
	u8 *reg1 = buf + 4;
	u8 *reg2 = buf;

	sja1105_packing(reg1, &entry->speed,     30, 29, size, op);
	sja1105_packing(reg1, &entry->drpdtag,   23, 23, size, op);
	sja1105_packing(reg1, &entry->drpuntag,  22, 22, size, op);
	sja1105_packing(reg1, &entry->retag,     21, 21, size, op);
	sja1105_packing(reg1, &entry->dyn_learn, 20, 20, size, op);
	sja1105_packing(reg1, &entry->egress,    19, 19, size, op);
	sja1105_packing(reg1, &entry->ingress,   18, 18, size, op);
	sja1105_packing(reg1, &entry->ing_mirr,  17, 17, size, op);
	sja1105_packing(reg1, &entry->egr_mirr,  16, 16, size, op);
	sja1105_packing(reg1, &entry->vlanprio,  14, 12, size, op);
	sja1105_packing(reg1, &entry->vlanid,    11,  0, size, op);
	sja1105_packing(reg2, &entry->tp_delin,  31, 16, size, op);
	sja1105_packing(reg2, &entry->tp_delout, 15,  0, size, op);
	/* MAC configuration table entries which can't be reconfigured:
	 * top, base, enabled, ifg, maxage, drpnona664
	 */
	/* Bogus return value, not used anywhere */
	return 0;
}

static void
sja1105pqrs_mac_config_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				   enum packing_op op)
{
	const int size = SJA1105ET_SIZE_MAC_CONFIG_DYN_ENTRY;
	u8 *p = buf + SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY;

	sja1105_packing(p, &cmd->valid,   31, 31, size, op);
	sja1105_packing(p, &cmd->errors,  30, 30, size, op);
	sja1105_packing(p, &cmd->rdwrset, 29, 29, size, op);
	sja1105_packing(p, &cmd->index,    2,  0, size, op);
}

static void
sja1105et_l2_lookup_params_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				       enum packing_op op)
{
	sja1105_packing(buf, &cmd->valid, 31, 31,
			SJA1105ET_SIZE_L2_LOOKUP_PARAMS_DYN_CMD, op);
}

static size_t
sja1105et_l2_lookup_params_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op)
{
	struct sja1105_l2_lookup_params_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->poly, 7, 0,
			SJA1105ET_SIZE_L2_LOOKUP_PARAMS_DYN_CMD, op);
	/* Bogus return value, not used anywhere */
	return 0;
}

static void
sja1105pqrs_l2_lookup_params_cmd_packing(void *buf,
					 struct sja1105_dyn_cmd *cmd,
					 enum packing_op op)
{
	u8 *p = buf + SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,   31, 31, size, op);
	sja1105_packing(p, &cmd->rdwrset, 30, 30, size, op);
}

static void
sja1105et_general_params_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				     enum packing_op op)
{
	const int size = SJA1105ET_SIZE_GENERAL_PARAMS_DYN_CMD;

	sja1105_packing(buf, &cmd->valid,  31, 31, size, op);
	sja1105_packing(buf, &cmd->errors, 30, 30, size, op);
}

static size_t
sja1105et_general_params_entry_packing(void *buf, void *entry_ptr,
				       enum packing_op op)
{
	struct sja1105_general_params_entry *entry = entry_ptr;
	const int size = SJA1105ET_SIZE_GENERAL_PARAMS_DYN_CMD;

	sja1105_packing(buf, &entry->mirr_port, 2, 0, size, op);
	/* Bogus return value, not used anywhere */
	return 0;
}

static void
sja1105pqrs_general_params_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				       enum packing_op op)
{
	u8 *p = buf + SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,   31, 31, size, op);
	sja1105_packing(p, &cmd->errors,  30, 30, size, op);
	sja1105_packing(p, &cmd->rdwrset, 28, 28, size, op);
}

static void
sja1105pqrs_avb_params_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				   enum packing_op op)
{
	u8 *p = buf + SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,   31, 31, size, op);
	sja1105_packing(p, &cmd->errors,  30, 30, size, op);
	sja1105_packing(p, &cmd->rdwrset, 29, 29, size, op);
}

static void
sja1105_retagging_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
			      enum packing_op op)
{
	u8 *p = buf + SJA1105_SIZE_RETAGGING_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,    31, 31, size, op);
	sja1105_packing(p, &cmd->errors,   30, 30, size, op);
	sja1105_packing(p, &cmd->valident, 29, 29, size, op);
	sja1105_packing(p, &cmd->rdwrset,  28, 28, size, op);
	sja1105_packing(p, &cmd->index,     5,  0, size, op);
}

static void sja1105et_cbs_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
				      enum packing_op op)
{
	u8 *p = buf + SJA1105ET_SIZE_CBS_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid, 31, 31, size, op);
	sja1105_packing(p, &cmd->index, 19, 16, size, op);
}

static size_t sja1105et_cbs_entry_packing(void *buf, void *entry_ptr,
					  enum packing_op op)
{
	const size_t size = SJA1105ET_SIZE_CBS_ENTRY;
	struct sja1105_cbs_entry *entry = entry_ptr;
	u8 *cmd = buf + size;
	u32 *p = buf;

	sja1105_packing(cmd, &entry->port, 5, 3, SJA1105_SIZE_DYN_CMD, op);
	sja1105_packing(cmd, &entry->prio, 2, 0, SJA1105_SIZE_DYN_CMD, op);
	sja1105_packing(p + 3, &entry->credit_lo,  31, 0, size, op);
	sja1105_packing(p + 2, &entry->credit_hi,  31, 0, size, op);
	sja1105_packing(p + 1, &entry->send_slope, 31, 0, size, op);
	sja1105_packing(p + 0, &entry->idle_slope, 31, 0, size, op);
	return size;
}

static void sja1105pqrs_cbs_cmd_packing(void *buf, struct sja1105_dyn_cmd *cmd,
					enum packing_op op)
{
	u8 *p = buf + SJA1105PQRS_SIZE_CBS_ENTRY;
	const int size = SJA1105_SIZE_DYN_CMD;

	sja1105_packing(p, &cmd->valid,   31, 31, size, op);
	sja1105_packing(p, &cmd->rdwrset, 30, 30, size, op);
	sja1105_packing(p, &cmd->errors,  29, 29, size, op);
	sja1105_packing(p, &cmd->index,    3,  0, size, op);
}

static size_t sja1105pqrs_cbs_entry_packing(void *buf, void *entry_ptr,
					    enum packing_op op)
{
	const size_t size = SJA1105PQRS_SIZE_CBS_ENTRY;
	struct sja1105_cbs_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->port,      159, 157, size, op);
	sja1105_packing(buf, &entry->prio,      156, 154, size, op);
	sja1105_packing(buf, &entry->credit_lo, 153, 122, size, op);
	sja1105_packing(buf, &entry->credit_hi, 121,  90, size, op);
	sja1105_packing(buf, &entry->send_slope, 89,  58, size, op);
	sja1105_packing(buf, &entry->idle_slope, 57,  26, size, op);
	return size;
}

#define OP_READ		BIT(0)
#define OP_WRITE	BIT(1)
#define OP_DEL		BIT(2)
#define OP_SEARCH	BIT(3)

/* SJA1105E/T: First generation */
const struct sja1105_dynamic_table_ops sja1105et_dyn_ops[BLK_IDX_MAX_DYN] = {
	[BLK_IDX_VL_LOOKUP] = {
		.entry_packing = sja1105et_vl_lookup_entry_packing,
		.cmd_packing = sja1105_vl_lookup_cmd_packing,
		.access = OP_WRITE,
		.max_entry_count = SJA1105_MAX_VL_LOOKUP_COUNT,
		.packed_size = SJA1105ET_SIZE_VL_LOOKUP_DYN_CMD,
		.addr = 0x35,
	},
	[BLK_IDX_L2_LOOKUP] = {
		.entry_packing = sja1105et_dyn_l2_lookup_entry_packing,
		.cmd_packing = sja1105et_l2_lookup_cmd_packing,
		.access = (OP_READ | OP_WRITE | OP_DEL),
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
		.packed_size = SJA1105ET_SIZE_L2_LOOKUP_DYN_CMD,
		.addr = 0x20,
	},
	[BLK_IDX_MGMT_ROUTE] = {
		.entry_packing = sja1105et_mgmt_route_entry_packing,
		.cmd_packing = sja1105et_mgmt_route_cmd_packing,
		.access = (OP_READ | OP_WRITE),
		.max_entry_count = SJA1105_NUM_PORTS,
		.packed_size = SJA1105ET_SIZE_L2_LOOKUP_DYN_CMD,
		.addr = 0x20,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.entry_packing = sja1105_vlan_lookup_entry_packing,
		.cmd_packing = sja1105_vlan_lookup_cmd_packing,
		.access = (OP_WRITE | OP_DEL),
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
		.packed_size = SJA1105_SIZE_VLAN_LOOKUP_DYN_CMD,
		.addr = 0x27,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.entry_packing = sja1105_l2_forwarding_entry_packing,
		.cmd_packing = sja1105_l2_forwarding_cmd_packing,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
		.access = OP_WRITE,
		.packed_size = SJA1105_SIZE_L2_FORWARDING_DYN_CMD,
		.addr = 0x24,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.entry_packing = sja1105et_mac_config_entry_packing,
		.cmd_packing = sja1105et_mac_config_cmd_packing,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
		.access = OP_WRITE,
		.packed_size = SJA1105ET_SIZE_MAC_CONFIG_DYN_CMD,
		.addr = 0x36,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.entry_packing = sja1105et_l2_lookup_params_entry_packing,
		.cmd_packing = sja1105et_l2_lookup_params_cmd_packing,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
		.access = OP_WRITE,
		.packed_size = SJA1105ET_SIZE_L2_LOOKUP_PARAMS_DYN_CMD,
		.addr = 0x38,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.entry_packing = sja1105et_general_params_entry_packing,
		.cmd_packing = sja1105et_general_params_cmd_packing,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
		.access = OP_WRITE,
		.packed_size = SJA1105ET_SIZE_GENERAL_PARAMS_DYN_CMD,
		.addr = 0x34,
	},
	[BLK_IDX_RETAGGING] = {
		.entry_packing = sja1105_retagging_entry_packing,
		.cmd_packing = sja1105_retagging_cmd_packing,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
		.access = (OP_WRITE | OP_DEL),
		.packed_size = SJA1105_SIZE_RETAGGING_DYN_CMD,
		.addr = 0x31,
	},
	[BLK_IDX_CBS] = {
		.entry_packing = sja1105et_cbs_entry_packing,
		.cmd_packing = sja1105et_cbs_cmd_packing,
		.max_entry_count = SJA1105ET_MAX_CBS_COUNT,
		.access = OP_WRITE,
		.packed_size = SJA1105ET_SIZE_CBS_DYN_CMD,
		.addr = 0x2c,
	},
};

/* SJA1105P/Q/R/S: Second generation */
const struct sja1105_dynamic_table_ops sja1105pqrs_dyn_ops[BLK_IDX_MAX_DYN] = {
	[BLK_IDX_VL_LOOKUP] = {
		.entry_packing = sja1105_vl_lookup_entry_packing,
		.cmd_packing = sja1105_vl_lookup_cmd_packing,
		.access = (OP_READ | OP_WRITE),
		.max_entry_count = SJA1105_MAX_VL_LOOKUP_COUNT,
		.packed_size = SJA1105PQRS_SIZE_VL_LOOKUP_DYN_CMD,
		.addr = 0x47,
	},
	[BLK_IDX_L2_LOOKUP] = {
		.entry_packing = sja1105pqrs_dyn_l2_lookup_entry_packing,
		.cmd_packing = sja1105pqrs_l2_lookup_cmd_packing,
		.access = (OP_READ | OP_WRITE | OP_DEL | OP_SEARCH),
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
		.packed_size = SJA1105PQRS_SIZE_L2_LOOKUP_DYN_CMD,
		.addr = 0x24,
	},
	[BLK_IDX_MGMT_ROUTE] = {
		.entry_packing = sja1105pqrs_mgmt_route_entry_packing,
		.cmd_packing = sja1105pqrs_mgmt_route_cmd_packing,
		.access = (OP_READ | OP_WRITE | OP_DEL | OP_SEARCH),
		.max_entry_count = SJA1105_NUM_PORTS,
		.packed_size = SJA1105PQRS_SIZE_L2_LOOKUP_DYN_CMD,
		.addr = 0x24,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.entry_packing = sja1105_vlan_lookup_entry_packing,
		.cmd_packing = sja1105_vlan_lookup_cmd_packing,
		.access = (OP_READ | OP_WRITE | OP_DEL),
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
		.packed_size = SJA1105_SIZE_VLAN_LOOKUP_DYN_CMD,
		.addr = 0x2D,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.entry_packing = sja1105_l2_forwarding_entry_packing,
		.cmd_packing = sja1105_l2_forwarding_cmd_packing,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
		.access = OP_WRITE,
		.packed_size = SJA1105_SIZE_L2_FORWARDING_DYN_CMD,
		.addr = 0x2A,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.entry_packing = sja1105pqrs_mac_config_entry_packing,
		.cmd_packing = sja1105pqrs_mac_config_cmd_packing,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
		.access = (OP_READ | OP_WRITE),
		.packed_size = SJA1105PQRS_SIZE_MAC_CONFIG_DYN_CMD,
		.addr = 0x4B,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.entry_packing = sja1105pqrs_l2_lookup_params_entry_packing,
		.cmd_packing = sja1105pqrs_l2_lookup_params_cmd_packing,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
		.access = (OP_READ | OP_WRITE),
		.packed_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_DYN_CMD,
		.addr = 0x54,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.entry_packing = sja1105pqrs_avb_params_entry_packing,
		.cmd_packing = sja1105pqrs_avb_params_cmd_packing,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
		.access = (OP_READ | OP_WRITE),
		.packed_size = SJA1105PQRS_SIZE_AVB_PARAMS_DYN_CMD,
		.addr = 0x8003,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.entry_packing = sja1105pqrs_general_params_entry_packing,
		.cmd_packing = sja1105pqrs_general_params_cmd_packing,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
		.access = (OP_READ | OP_WRITE),
		.packed_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_DYN_CMD,
		.addr = 0x3B,
	},
	[BLK_IDX_RETAGGING] = {
		.entry_packing = sja1105_retagging_entry_packing,
		.cmd_packing = sja1105_retagging_cmd_packing,
		.max_entry_count = SJA1105_MAX_RETAGGING_COUNT,
		.access = (OP_READ | OP_WRITE | OP_DEL),
		.packed_size = SJA1105_SIZE_RETAGGING_DYN_CMD,
		.addr = 0x38,
	},
	[BLK_IDX_CBS] = {
		.entry_packing = sja1105pqrs_cbs_entry_packing,
		.cmd_packing = sja1105pqrs_cbs_cmd_packing,
		.max_entry_count = SJA1105PQRS_MAX_CBS_COUNT,
		.access = OP_WRITE,
		.packed_size = SJA1105PQRS_SIZE_CBS_DYN_CMD,
		.addr = 0x32,
	},
};

/* Provides read access to the settings through the dynamic interface
 * of the switch.
 * @blk_idx	is used as key to select from the sja1105_dynamic_table_ops.
 *		The selection is limited by the hardware in respect to which
 *		configuration blocks can be read through the dynamic interface.
 * @index	is used to retrieve a particular table entry. If negative,
 *		(and if the @blk_idx supports the searching operation) a search
 *		is performed by the @entry parameter.
 * @entry	Type-casted to an unpacked structure that holds a table entry
 *		of the type specified in @blk_idx.
 *		Usually an output argument. If @index is negative, then this
 *		argument is used as input/output: it should be pre-populated
 *		with the element to search for. Entries which support the
 *		search operation will have an "index" field (not the @index
 *		argument to this function) and that is where the found index
 *		will be returned (or left unmodified - thus negative - if not
 *		found).
 */
int sja1105_dynamic_config_read(struct sja1105_private *priv,
				enum sja1105_blk_idx blk_idx,
				int index, void *entry)
{
	const struct sja1105_dynamic_table_ops *ops;
	struct sja1105_dyn_cmd cmd = {0};
	/* SPI payload buffer */
	u8 packed_buf[SJA1105_MAX_DYN_CMD_SIZE] = {0};
	int retries = 3;
	int rc;

	if (blk_idx >= BLK_IDX_MAX_DYN)
		return -ERANGE;

	ops = &priv->info->dyn_ops[blk_idx];

	if (index >= 0 && index >= ops->max_entry_count)
		return -ERANGE;
	if (index < 0 && !(ops->access & OP_SEARCH))
		return -EOPNOTSUPP;
	if (!(ops->access & OP_READ))
		return -EOPNOTSUPP;
	if (ops->packed_size > SJA1105_MAX_DYN_CMD_SIZE)
		return -ERANGE;
	if (!ops->cmd_packing)
		return -EOPNOTSUPP;
	if (!ops->entry_packing)
		return -EOPNOTSUPP;

	cmd.valid = true; /* Trigger action on table entry */
	cmd.rdwrset = SPI_READ; /* Action is read */
	if (index < 0) {
		/* Avoid copying a signed negative number to an u64 */
		cmd.index = 0;
		cmd.search = true;
	} else {
		cmd.index = index;
		cmd.search = false;
	}
	cmd.valident = true;
	ops->cmd_packing(packed_buf, &cmd, PACK);

	if (cmd.search)
		ops->entry_packing(packed_buf, entry, PACK);

	/* Send SPI write operation: read config table entry */
	rc = sja1105_xfer_buf(priv, SPI_WRITE, ops->addr, packed_buf,
			      ops->packed_size);
	if (rc < 0)
		return rc;

	/* Loop until we have confirmation that hardware has finished
	 * processing the command and has cleared the VALID field
	 */
	do {
		memset(packed_buf, 0, ops->packed_size);

		/* Retrieve the read operation's result */
		rc = sja1105_xfer_buf(priv, SPI_READ, ops->addr, packed_buf,
				      ops->packed_size);
		if (rc < 0)
			return rc;

		cmd = (struct sja1105_dyn_cmd) {0};
		ops->cmd_packing(packed_buf, &cmd, UNPACK);
		/* UM10944: [valident] will always be found cleared
		 * during a read access with MGMTROUTE set.
		 * So don't error out in that case.
		 */
		if (!cmd.valident && blk_idx != BLK_IDX_MGMT_ROUTE)
			return -ENOENT;
		cpu_relax();
	} while (cmd.valid && --retries);

	if (cmd.valid)
		return -ETIMEDOUT;

	/* Don't dereference possibly NULL pointer - maybe caller
	 * only wanted to see whether the entry existed or not.
	 */
	if (entry)
		ops->entry_packing(packed_buf, entry, UNPACK);
	return 0;
}

int sja1105_dynamic_config_write(struct sja1105_private *priv,
				 enum sja1105_blk_idx blk_idx,
				 int index, void *entry, bool keep)
{
	const struct sja1105_dynamic_table_ops *ops;
	struct sja1105_dyn_cmd cmd = {0};
	/* SPI payload buffer */
	u8 packed_buf[SJA1105_MAX_DYN_CMD_SIZE] = {0};
	int rc;

	if (blk_idx >= BLK_IDX_MAX_DYN)
		return -ERANGE;

	ops = &priv->info->dyn_ops[blk_idx];

	if (index >= ops->max_entry_count)
		return -ERANGE;
	if (index < 0)
		return -ERANGE;
	if (!(ops->access & OP_WRITE))
		return -EOPNOTSUPP;
	if (!keep && !(ops->access & OP_DEL))
		return -EOPNOTSUPP;
	if (ops->packed_size > SJA1105_MAX_DYN_CMD_SIZE)
		return -ERANGE;

	cmd.valident = keep; /* If false, deletes entry */
	cmd.valid = true; /* Trigger action on table entry */
	cmd.rdwrset = SPI_WRITE; /* Action is write */
	cmd.index = index;

	if (!ops->cmd_packing)
		return -EOPNOTSUPP;
	ops->cmd_packing(packed_buf, &cmd, PACK);

	if (!ops->entry_packing)
		return -EOPNOTSUPP;
	/* Don't dereference potentially NULL pointer if just
	 * deleting a table entry is what was requested. For cases
	 * where 'index' field is physically part of entry structure,
	 * and needed here, we deal with that in the cmd_packing callback.
	 */
	if (keep)
		ops->entry_packing(packed_buf, entry, PACK);

	/* Send SPI write operation: read config table entry */
	rc = sja1105_xfer_buf(priv, SPI_WRITE, ops->addr, packed_buf,
			      ops->packed_size);
	if (rc < 0)
		return rc;

	cmd = (struct sja1105_dyn_cmd) {0};
	ops->cmd_packing(packed_buf, &cmd, UNPACK);
	if (cmd.errors)
		return -EINVAL;

	return 0;
}

static u8 sja1105_crc8_add(u8 crc, u8 byte, u8 poly)
{
	int i;

	for (i = 0; i < 8; i++) {
		if ((crc ^ byte) & (1 << 7)) {
			crc <<= 1;
			crc ^= poly;
		} else {
			crc <<= 1;
		}
		byte <<= 1;
	}
	return crc;
}

/* CRC8 algorithm with non-reversed input, non-reversed output,
 * no input xor and no output xor. Code customized for receiving
 * the SJA1105 E/T FDB keys (vlanid, macaddr) as input. CRC polynomial
 * is also received as argument in the Koopman notation that the switch
 * hardware stores it in.
 */
u8 sja1105et_fdb_hash(struct sja1105_private *priv, const u8 *addr, u16 vid)
{
	struct sja1105_l2_lookup_params_entry *l2_lookup_params =
		priv->static_config.tables[BLK_IDX_L2_LOOKUP_PARAMS].entries;
	u64 poly_koopman = l2_lookup_params->poly;
	/* Convert polynomial from Koopman to 'normal' notation */
	u8 poly = (u8)(1 + (poly_koopman << 1));
	u64 vlanid = l2_lookup_params->shared_learn ? 0 : vid;
	u64 input = (vlanid << 48) | ether_addr_to_u64(addr);
	u8 crc = 0; /* seed */
	int i;

	/* Mask the eight bytes starting from MSB one at a time */
	for (i = 56; i >= 0; i -= 8) {
		u8 byte = (input & (0xffull << i)) >> i;

		crc = sja1105_crc8_add(crc, byte, poly);
	}
	return crc;
}
