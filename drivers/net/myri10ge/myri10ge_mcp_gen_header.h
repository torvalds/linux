#ifndef __MYRI10GE_MCP_GEN_HEADER_H__
#define __MYRI10GE_MCP_GEN_HEADER_H__


#define MCP_HEADER_PTR_OFFSET  0x3c

#define MCP_TYPE_MX 0x4d582020	/* "MX  " */
#define MCP_TYPE_PCIE 0x70636965	/* "PCIE" pcie-only MCP */
#define MCP_TYPE_ETH 0x45544820	/* "ETH " */
#define MCP_TYPE_MCP0 0x4d435030	/* "MCP0" */
#define MCP_TYPE_DFLT 0x20202020	/* "    " */

struct mcp_gen_header {
	/* the first 4 fields are filled at compile time */
	unsigned header_length;
	__be32 mcp_type;
	char version[128];
	unsigned mcp_private;	/* pointer to mcp-type specific structure */

	/* filled by the MCP at run-time */
	unsigned sram_size;
	unsigned string_specs;	/* either the original STRING_SPECS or a superset */
	unsigned string_specs_len;

	/* Fields above this comment are guaranteed to be present.
	 *
	 * Fields below this comment are extensions added in later versions
	 * of this struct, drivers should compare the header_length against
	 * offsetof(field) to check wether a given MCP implements them.
	 *
	 * Never remove any field.  Keep everything naturally align.
	 */

	/* Specifies if the running mcp is mcp0, 1, or 2. */
	unsigned char mcp_index;
	unsigned char disable_rabbit;
	unsigned char unaligned_tlp;
	unsigned char pad1;
	unsigned counters_addr;
	unsigned copy_block_info;	/* for small mcps loaded with "lload -d" */
	unsigned short handoff_id_major;	/* must be equal */
	unsigned short handoff_id_caps;	/* bitfield: new mcp must have superset */
	unsigned msix_table_addr;	/* start address of msix table in firmware */
	/* 8 */
};

#endif				/* __MYRI10GE_MCP_GEN_HEADER_H__ */
