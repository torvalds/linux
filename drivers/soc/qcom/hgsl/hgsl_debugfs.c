// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/platform_device.h>
#include <linux/debugfs.h>

#include "hgsl.h"
#include "hgsl_debugfs.h"

static int hgsl_client_mem_show(struct seq_file *s, void *unused)
{
	struct hgsl_priv *priv = s->private;
	struct hgsl_mem_node *tmp = NULL;
	struct rb_node *rb = NULL;

	seq_printf(s, "%16s %16s %10s %10s\n",
			"gpuaddr", "size", "flags", "type");

	mutex_lock(&priv->lock);
	for (rb = rb_first(&priv->mem_allocated); rb; rb = rb_next(rb)) {
		tmp = rb_entry(rb, struct hgsl_mem_node, mem_rb_node);
		seq_printf(s, "%p %16llx %10x %10d\n",
				tmp->memdesc.gpuaddr,
				tmp->memdesc.size,
				tmp->flags,
				tmp->memtype
				);
	}
	mutex_unlock(&priv->lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hgsl_client_mem);

static int hgsl_client_memtype_show(struct seq_file *s, void *unused)
{
	struct hgsl_priv *priv = s->private;
	struct hgsl_mem_node *tmp = NULL;
	struct rb_node *rb = NULL;
	int i;
	int memtype;

	static struct {
		char *name;
		size_t size;
	} gpu_mem_types[] = {
		{"any", 0},
		{"framebuffer", 0},
		{"renderbbuffer", 0},
		{"arraybuffer", 0},
		{"elementarraybuffer", 0},
		{"vertexarraybuffer", 0},
		{"texture", 0},
		{"surface", 0},
		{"eglsurface", 0},
		{"gl", 0},
		{"cl", 0},
		{"cl_buffer_map", 0},
		{"cl_buffer_unmap", 0},
		{"cl_image_map", 0},
		{"cl_image_unmap", 0},
		{"cl_kernel_stack", 0},
		{"cmds", 0},
		{"2d", 0},
		{"egl_image", 0},
		{"egl_shadow", 0},
		{"multisample", 0},
		{"2d_ext", 0},
		{"3d_ext", 0}, /* 0x16 */
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"unknown_type", 0},
		{"vk_any", 0}, /* 0x20 */
		{"vk_instance", 0},
		{"vk_physicaldevice", 0},
		{"vk_device", 0},
		{"vk_queue", 0},
		{"vk_cmdbuffer", 0},
		{"vk_devicememory", 0},
		{"vk_buffer", 0},
		{"vk_bufferview", 0},
		{"vk_image", 0},
		{"vk_imageview", 0},
		{"vk_shadermodule", 0},
		{"vk_pipeline", 0},
		{"vk_pipelinecache", 0},
		{"vk_pipelinelayout", 0},
		{"vk_sampler", 0},
		{"vk_samplerycbcrconversionkhr", 0}, /* 0x30 */
		{"vk_descriptorset", 0},
		{"vk_descriptorsetlayout", 0},
		{"vk_descriptorpool", 0},
		{"vk_fence", 0},
		{"vk_semaphore", 0},
		{"vk_event", 0},
		{"vk_querypool", 0},
		{"vk_framebuffer", 0},
		{"vk_renderpass", 0},
		{"vk_program", 0},
		{"vk_commandpool", 0},
		{"vk_surfacekhr", 0},
		{"vk_swapchainkhr", 0},
		{"vk_descriptorupdatetemplate", 0},
		{"vk_deferredoperationkhr", 0},
		{"vk_privatedataslotext", 0}, /* 0x40 */
		{"vk_debug_utils", 0},
		{"vk_tensor", 0},
		{"vk_tensorview", 0},
		{"vk_mlpipeline", 0},
		{"vk_acceleration_structure", 0},
	};

	for (i = 0; i < ARRAY_SIZE(gpu_mem_types); i++)
		gpu_mem_types[i].size = 0;

	mutex_lock(&priv->lock);
	for (rb = rb_first(&priv->mem_allocated); rb; rb = rb_next(rb)) {
		tmp = rb_entry(rb, struct hgsl_mem_node, mem_rb_node);
		memtype = GET_MEMTYPE(tmp->flags);
		if (memtype < ARRAY_SIZE(gpu_mem_types))
			gpu_mem_types[memtype].size += tmp->memdesc.size;
	}
	mutex_unlock(&priv->lock);

	seq_printf(s, "%16s %16s\n", "type", "size");
	for (i = 0; i < ARRAY_SIZE(gpu_mem_types); i++) {
		if (gpu_mem_types[i].size != 0)
			seq_printf(s, "%16s %16d\n",
					gpu_mem_types[i].name,
					gpu_mem_types[i].size);
	}


	return 0;
}
DEFINE_SHOW_ATTRIBUTE(hgsl_client_memtype);

int hgsl_debugfs_client_init(struct hgsl_priv *priv)
{
	struct qcom_hgsl *hgsl = priv->dev;
	unsigned char name[16];
	struct dentry *ret;

	snprintf(name, sizeof(name), "%d", priv->pid);
	ret = debugfs_create_dir(name,
				hgsl->clients_debugfs);
	if (IS_ERR(ret)) {
		pr_warn("Create debugfs proc node failed.\n");
		priv->debugfs_client = NULL;
		return PTR_ERR(ret);
	} else
		priv->debugfs_client = ret;

	priv->debugfs_mem = debugfs_create_file("mem", 0444,
			priv->debugfs_client,
			priv,
			&hgsl_client_mem_fops);

	priv->debugfs_memtype = debugfs_create_file("obj_types", 0444,
			priv->debugfs_client,
			priv,
			&hgsl_client_memtype_fops);

	return 0;
}

void hgsl_debugfs_client_release(struct hgsl_priv *priv)
{
	debugfs_remove_recursive(priv->debugfs_client);
}

void hgsl_debugfs_init(struct platform_device *pdev)
{
	struct qcom_hgsl *hgsl = platform_get_drvdata(pdev);
	struct dentry *root;

	root = debugfs_create_dir("hgsl", NULL);

	hgsl->debugfs = root;
	hgsl->clients_debugfs = debugfs_create_dir("clients", root);
}

void hgsl_debugfs_release(struct platform_device *pdev)
{
	struct qcom_hgsl *hgsl = platform_get_drvdata(pdev);

	debugfs_remove_recursive(hgsl->debugfs);
}
