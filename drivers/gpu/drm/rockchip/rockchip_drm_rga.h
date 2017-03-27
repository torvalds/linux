#ifndef __ROCKCHIP_DRM_RGA__
#define __ROCKCHIP_DRM_RGA__

#define RGA_CMDBUF_SIZE			14
#define RGA_CMDLIST_SIZE		0x20
#define RGA_CMDLIST_NUM			64

/* cmdlist data structure */
struct rga_cmdlist {
	u32		head;
	u32		data[(RGA_CMDLIST_SIZE + RGA_CMDBUF_SIZE) * 2];
	u32		last;	/* last data offset */
	void		*src_mmu_pages;
	void		*dst_mmu_pages;
	void		*src1_mmu_pages;
	struct dma_buf_attachment *src_attach;
	struct dma_buf_attachment *src1_attach;
	struct dma_buf_attachment *dst_attach;
};

struct rga_cmdlist_node {
	struct list_head	list;
	struct rga_cmdlist	cmdlist;
};

struct rga_runqueue_node {
	struct list_head	list;

	struct drm_device	*drm_dev;
	struct device		*dev;
	pid_t			pid;
	struct drm_file		*file;
	struct completion	complete;

	struct list_head	run_cmdlist;

	int			cmdlist_cnt;
	void			*cmdlist_pool_virt;
	dma_addr_t		cmdlist_pool;
	struct dma_attrs	cmdlist_dma_attrs;
};

struct rockchip_rga_version {
	u32			major;
	u32			minor;
};

struct rockchip_rga {
	struct drm_device	*drm_dev;
	struct device		*dev;
	struct regmap		*grf;
	void __iomem		*regs;
	struct clk		*sclk;
	struct clk		*aclk;
	struct clk		*hclk;

	bool				suspended;
	struct rockchip_rga_version	version;
	struct drm_rockchip_subdrv	subdrv;
	struct workqueue_struct		*rga_workq;
	struct work_struct		runqueue_work;

	/* rga command list pool */
	struct rga_cmdlist_node		cmdlist_node[RGA_CMDLIST_NUM];
	struct mutex			cmdlist_mutex;

	struct list_head		free_cmdlist;

	/* rga runqueue */
	struct rga_runqueue_node	*runqueue_node;
	struct list_head		runqueue_list;
	struct mutex			runqueue_mutex;
	struct kmem_cache		*runqueue_slab;
};

struct rockchip_drm_rga_private {
	struct device		*dev;
	struct list_head	inuse_cmdlist;
	struct list_head	userptr_list;
};

#ifdef CONFIG_ROCKCHIP_DRM_RGA
int rockchip_rga_get_ver_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
int rockchip_rga_set_cmdlist_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_priv);
int rockchip_rga_exec_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
#else
static inline int rockchip_rga_get_ver_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file_priv)
{
	return -ENODEV;
}

static inline int rockchip_rga_set_cmdlist_ioctl(struct drm_device *dev,
						 void *data,
						 struct drm_file *file_priv)
{
	return -ENODEV;
}

static inline int rockchip_rga_exec_ioctl(struct drm_device *dev, void *data,
					  struct drm_file *file_priv)
{
	return -ENODEV;
}
#endif

#endif /* __ROCKCHIP_DRM_RGA__ */
