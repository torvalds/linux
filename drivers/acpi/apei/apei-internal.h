/*
 * apei-internal.h - ACPI Platform Error Interface internal
 * definations.
 */

#ifndef APEI_INTERNAL_H
#define APEI_INTERNAL_H

#include <linux/cper.h>
#include <linux/acpi.h>
#include <linux/acpi_io.h>

struct apei_exec_context;

typedef int (*apei_exec_ins_func_t)(struct apei_exec_context *ctx,
				    struct acpi_whea_header *entry);

#define APEI_EXEC_INS_ACCESS_REGISTER	0x0001

struct apei_exec_ins_type {
	u32 flags;
	apei_exec_ins_func_t run;
};

struct apei_exec_context {
	u32 ip;
	u64 value;
	u64 var1;
	u64 var2;
	u64 src_base;
	u64 dst_base;
	struct apei_exec_ins_type *ins_table;
	u32 instructions;
	struct acpi_whea_header *action_table;
	u32 entries;
};

void apei_exec_ctx_init(struct apei_exec_context *ctx,
			struct apei_exec_ins_type *ins_table,
			u32 instructions,
			struct acpi_whea_header *action_table,
			u32 entries);

static inline void apei_exec_ctx_set_input(struct apei_exec_context *ctx,
					   u64 input)
{
	ctx->value = input;
}

static inline u64 apei_exec_ctx_get_output(struct apei_exec_context *ctx)
{
	return ctx->value;
}

int __apei_exec_run(struct apei_exec_context *ctx, u8 action, bool optional);

static inline int apei_exec_run(struct apei_exec_context *ctx, u8 action)
{
	return __apei_exec_run(ctx, action, 0);
}

/* It is optional whether the firmware provides the action */
static inline int apei_exec_run_optional(struct apei_exec_context *ctx, u8 action)
{
	return __apei_exec_run(ctx, action, 1);
}

/* Common instruction implementation */

/* IP has been set in instruction function */
#define APEI_EXEC_SET_IP	1

int apei_map_generic_address(struct acpi_generic_address *reg);

static inline void apei_unmap_generic_address(struct acpi_generic_address *reg)
{
	acpi_os_unmap_generic_address(reg);
}

int apei_read(u64 *val, struct acpi_generic_address *reg);
int apei_write(u64 val, struct acpi_generic_address *reg);

int __apei_exec_read_register(struct acpi_whea_header *entry, u64 *val);
int __apei_exec_write_register(struct acpi_whea_header *entry, u64 val);
int apei_exec_read_register(struct apei_exec_context *ctx,
			    struct acpi_whea_header *entry);
int apei_exec_read_register_value(struct apei_exec_context *ctx,
				  struct acpi_whea_header *entry);
int apei_exec_write_register(struct apei_exec_context *ctx,
			     struct acpi_whea_header *entry);
int apei_exec_write_register_value(struct apei_exec_context *ctx,
				   struct acpi_whea_header *entry);
int apei_exec_noop(struct apei_exec_context *ctx,
		   struct acpi_whea_header *entry);
int apei_exec_pre_map_gars(struct apei_exec_context *ctx);
int apei_exec_post_unmap_gars(struct apei_exec_context *ctx);

struct apei_resources {
	struct list_head iomem;
	struct list_head ioport;
};

static inline void apei_resources_init(struct apei_resources *resources)
{
	INIT_LIST_HEAD(&resources->iomem);
	INIT_LIST_HEAD(&resources->ioport);
}

void apei_resources_fini(struct apei_resources *resources);
int apei_resources_add(struct apei_resources *resources,
		       unsigned long start, unsigned long size,
		       bool iomem);
int apei_resources_sub(struct apei_resources *resources1,
		       struct apei_resources *resources2);
int apei_resources_request(struct apei_resources *resources,
			   const char *desc);
void apei_resources_release(struct apei_resources *resources);
int apei_exec_collect_resources(struct apei_exec_context *ctx,
				struct apei_resources *resources);

struct dentry;
struct dentry *apei_get_debugfs_dir(void);

#define apei_estatus_for_each_section(estatus, section)			\
	for (section = (struct acpi_hest_generic_data *)(estatus + 1);	\
	     (void *)section - (void *)estatus < estatus->data_length;	\
	     section = (void *)(section+1) + section->error_data_length)

static inline u32 apei_estatus_len(struct acpi_hest_generic_status *estatus)
{
	if (estatus->raw_data_length)
		return estatus->raw_data_offset + \
			estatus->raw_data_length;
	else
		return sizeof(*estatus) + estatus->data_length;
}

void apei_estatus_print(const char *pfx,
			const struct acpi_hest_generic_status *estatus);
int apei_estatus_check_header(const struct acpi_hest_generic_status *estatus);
int apei_estatus_check(const struct acpi_hest_generic_status *estatus);

int apei_osc_setup(void);
#endif
