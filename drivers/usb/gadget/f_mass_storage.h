#ifndef USB_F_MASS_STORAGE_H
#define USB_F_MASS_STORAGE_H

#include "storage_common.h"

struct fsg_module_parameters {
	char		*file[FSG_MAX_LUNS];
	bool		ro[FSG_MAX_LUNS];
	bool		removable[FSG_MAX_LUNS];
	bool		cdrom[FSG_MAX_LUNS];
	bool		nofua[FSG_MAX_LUNS];

	unsigned int	file_count, ro_count, removable_count, cdrom_count;
	unsigned int	nofua_count;
	unsigned int	luns;	/* nluns */
	bool		stall;	/* can_stall */
};

#define _FSG_MODULE_PARAM_ARRAY(prefix, params, name, type, desc)	\
	module_param_array_named(prefix ## name, params.name, type,	\
				 &prefix ## params.name ## _count,	\
				 S_IRUGO);				\
	MODULE_PARM_DESC(prefix ## name, desc)

#define _FSG_MODULE_PARAM(prefix, params, name, type, desc)		\
	module_param_named(prefix ## name, params.name, type,		\
			   S_IRUGO);					\
	MODULE_PARM_DESC(prefix ## name, desc)

#define __FSG_MODULE_PARAMETERS(prefix, params)				\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, file, charp,		\
				"names of backing files or devices");	\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, ro, bool,		\
				"true to force read-only");		\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, removable, bool,	\
				"true to simulate removable media");	\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, cdrom, bool,		\
				"true to simulate CD-ROM instead of disk"); \
	_FSG_MODULE_PARAM_ARRAY(prefix, params, nofua, bool,		\
				"true to ignore SCSI WRITE(10,12) FUA bit"); \
	_FSG_MODULE_PARAM(prefix, params, luns, uint,			\
			  "number of LUNs");				\
	_FSG_MODULE_PARAM(prefix, params, stall, bool,			\
			  "false to prevent bulk stalls")

#ifdef CONFIG_USB_GADGET_DEBUG_FILES

#define FSG_MODULE_PARAMETERS(prefix, params)				\
	__FSG_MODULE_PARAMETERS(prefix, params);			\
	module_param_named(num_buffers, fsg_num_buffers, uint, S_IRUGO);\
	MODULE_PARM_DESC(num_buffers, "Number of pipeline buffers")
#else

#define FSG_MODULE_PARAMETERS(prefix, params)				\
	__FSG_MODULE_PARAMETERS(prefix, params)

#endif

struct fsg_common;

/* FSF callback functions */
struct fsg_operations {
	/*
	 * Callback function to call when thread exits.  If no
	 * callback is set or it returns value lower then zero MSF
	 * will force eject all LUNs it operates on (including those
	 * marked as non-removable or with prevent_medium_removal flag
	 * set).
	 */
	int (*thread_exits)(struct fsg_common *common);
};

struct fsg_lun_config {
	const char *filename;
	char ro;
	char removable;
	char cdrom;
	char nofua;
};

struct fsg_config {
	unsigned nluns;
	struct fsg_lun_config luns[FSG_MAX_LUNS];

	/* Callback functions. */
	const struct fsg_operations	*ops;
	/* Gadget's private data. */
	void			*private_data;

	const char *vendor_name;		/*  8 characters or less */
	const char *product_name;		/* 16 characters or less */

	char			can_stall;
	unsigned int		fsg_num_buffers;
};

void fsg_common_get(struct fsg_common *common);

void fsg_common_put(struct fsg_common *common);

struct fsg_common *fsg_common_init(struct fsg_common *common,
				   struct usb_composite_dev *cdev,
				   struct fsg_config *cfg);

void fsg_common_set_sysfs(struct fsg_common *common, bool sysfs);

int fsg_common_set_num_buffers(struct fsg_common *common, unsigned int n);

int fsg_common_set_cdev(struct fsg_common *common,
			struct usb_composite_dev *cdev, bool can_stall);

void fsg_common_remove_lun(struct fsg_lun *lun, bool sysfs);

void fsg_common_remove_luns(struct fsg_common *common);

void fsg_common_free_luns(struct fsg_common *common);

int fsg_common_set_nluns(struct fsg_common *common, int nluns);

void fsg_config_from_params(struct fsg_config *cfg,
			    const struct fsg_module_parameters *params,
			    unsigned int fsg_num_buffers);

static inline struct fsg_common *
fsg_common_from_params(struct fsg_common *common,
		       struct usb_composite_dev *cdev,
		       const struct fsg_module_parameters *params,
		       unsigned int fsg_num_buffers)
	__attribute__((unused));
static inline struct fsg_common *
fsg_common_from_params(struct fsg_common *common,
		       struct usb_composite_dev *cdev,
		       const struct fsg_module_parameters *params,
		       unsigned int fsg_num_buffers)
{
	struct fsg_config cfg;
	fsg_config_from_params(&cfg, params, fsg_num_buffers);
	return fsg_common_init(common, cdev, &cfg);
}

#endif /* USB_F_MASS_STORAGE_H */
