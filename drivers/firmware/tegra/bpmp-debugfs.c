/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

struct seqbuf {
	char *buf;
	size_t pos;
	size_t size;
};

static void seqbuf_init(struct seqbuf *seqbuf, void *buf, size_t size)
{
	seqbuf->buf = buf;
	seqbuf->size = size;
	seqbuf->pos = 0;
}

static size_t seqbuf_avail(struct seqbuf *seqbuf)
{
	return seqbuf->pos < seqbuf->size ? seqbuf->size - seqbuf->pos : 0;
}

static size_t seqbuf_status(struct seqbuf *seqbuf)
{
	return seqbuf->pos <= seqbuf->size ? 0 : -EOVERFLOW;
}

static int seqbuf_eof(struct seqbuf *seqbuf)
{
	return seqbuf->pos >= seqbuf->size;
}

static int seqbuf_read(struct seqbuf *seqbuf, void *buf, size_t nbyte)
{
	nbyte = min(nbyte, seqbuf_avail(seqbuf));
	memcpy(buf, seqbuf->buf + seqbuf->pos, nbyte);
	seqbuf->pos += nbyte;
	return seqbuf_status(seqbuf);
}

static int seqbuf_read_u32(struct seqbuf *seqbuf, uint32_t *v)
{
	int err;

	err = seqbuf_read(seqbuf, v, 4);
	*v = le32_to_cpu(*v);
	return err;
}

static int seqbuf_read_str(struct seqbuf *seqbuf, const char **str)
{
	*str = seqbuf->buf + seqbuf->pos;
	seqbuf->pos += strnlen(*str, seqbuf_avail(seqbuf));
	seqbuf->pos++;
	return seqbuf_status(seqbuf);
}

static void seqbuf_seek(struct seqbuf *seqbuf, ssize_t offset)
{
	seqbuf->pos += offset;
}

/* map filename in Linux debugfs to corresponding entry in BPMP */
static const char *get_filename(struct tegra_bpmp *bpmp,
				const struct file *file, char *buf, int size)
{
	char root_path_buf[512];
	const char *root_path;
	const char *filename;
	size_t root_len;

	root_path = dentry_path(bpmp->debugfs_mirror, root_path_buf,
				sizeof(root_path_buf));
	if (IS_ERR(root_path))
		return NULL;

	root_len = strlen(root_path);

	filename = dentry_path(file->f_path.dentry, buf, size);
	if (IS_ERR(filename))
		return NULL;

	if (strlen(filename) < root_len ||
			strncmp(filename, root_path, root_len))
		return NULL;

	filename += root_len;

	return filename;
}

static int mrq_debugfs_read(struct tegra_bpmp *bpmp,
			    dma_addr_t name, size_t sz_name,
			    dma_addr_t data, size_t sz_data,
			    size_t *nbytes)
{
	struct mrq_debugfs_request req = {
		.cmd = cpu_to_le32(CMD_DEBUGFS_READ),
		.fop = {
			.fnameaddr = cpu_to_le32((uint32_t)name),
			.fnamelen = cpu_to_le32((uint32_t)sz_name),
			.dataaddr = cpu_to_le32((uint32_t)data),
			.datalen = cpu_to_le32((uint32_t)sz_data),
		},
	};
	struct mrq_debugfs_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_DEBUGFS,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};
	int err;

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err < 0)
		return err;

	*nbytes = (size_t)resp.fop.nbytes;

	return 0;
}

static int mrq_debugfs_write(struct tegra_bpmp *bpmp,
			     dma_addr_t name, size_t sz_name,
			     dma_addr_t data, size_t sz_data)
{
	const struct mrq_debugfs_request req = {
		.cmd = cpu_to_le32(CMD_DEBUGFS_WRITE),
		.fop = {
			.fnameaddr = cpu_to_le32((uint32_t)name),
			.fnamelen = cpu_to_le32((uint32_t)sz_name),
			.dataaddr = cpu_to_le32((uint32_t)data),
			.datalen = cpu_to_le32((uint32_t)sz_data),
		},
	};
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_DEBUGFS,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
	};

	return tegra_bpmp_transfer(bpmp, &msg);
}

static int mrq_debugfs_dumpdir(struct tegra_bpmp *bpmp, dma_addr_t addr,
			       size_t size, size_t *nbytes)
{
	const struct mrq_debugfs_request req = {
		.cmd = cpu_to_le32(CMD_DEBUGFS_DUMPDIR),
		.dumpdir = {
			.dataaddr = cpu_to_le32((uint32_t)addr),
			.datalen = cpu_to_le32((uint32_t)size),
		},
	};
	struct mrq_debugfs_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_DEBUGFS,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};
	int err;

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err < 0)
		return err;

	*nbytes = (size_t)resp.dumpdir.nbytes;

	return 0;
}

static int debugfs_show(struct seq_file *m, void *p)
{
	struct file *file = m->private;
	struct inode *inode = file_inode(file);
	struct tegra_bpmp *bpmp = inode->i_private;
	const size_t datasize = m->size;
	const size_t namesize = SZ_256;
	void *datavirt, *namevirt;
	dma_addr_t dataphys, namephys;
	char buf[256];
	const char *filename;
	size_t len, nbytes;
	int ret;

	filename = get_filename(bpmp, file, buf, sizeof(buf));
	if (!filename)
		return -ENOENT;

	namevirt = dma_alloc_coherent(bpmp->dev, namesize, &namephys,
				      GFP_KERNEL | GFP_DMA32);
	if (!namevirt)
		return -ENOMEM;

	datavirt = dma_alloc_coherent(bpmp->dev, datasize, &dataphys,
				      GFP_KERNEL | GFP_DMA32);
	if (!datavirt) {
		ret = -ENOMEM;
		goto free_namebuf;
	}

	len = strlen(filename);
	strncpy(namevirt, filename, namesize);

	ret = mrq_debugfs_read(bpmp, namephys, len, dataphys, datasize,
			       &nbytes);

	if (!ret)
		seq_write(m, datavirt, nbytes);

	dma_free_coherent(bpmp->dev, datasize, datavirt, dataphys);
free_namebuf:
	dma_free_coherent(bpmp->dev, namesize, namevirt, namephys);

	return ret;
}

static int debugfs_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, debugfs_show, file, SZ_128K);
}

static ssize_t debugfs_store(struct file *file, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct inode *inode = file_inode(file);
	struct tegra_bpmp *bpmp = inode->i_private;
	const size_t datasize = count;
	const size_t namesize = SZ_256;
	void *datavirt, *namevirt;
	dma_addr_t dataphys, namephys;
	char fnamebuf[256];
	const char *filename;
	size_t len;
	int ret;

	filename = get_filename(bpmp, file, fnamebuf, sizeof(fnamebuf));
	if (!filename)
		return -ENOENT;

	namevirt = dma_alloc_coherent(bpmp->dev, namesize, &namephys,
				      GFP_KERNEL | GFP_DMA32);
	if (!namevirt)
		return -ENOMEM;

	datavirt = dma_alloc_coherent(bpmp->dev, datasize, &dataphys,
				      GFP_KERNEL | GFP_DMA32);
	if (!datavirt) {
		ret = -ENOMEM;
		goto free_namebuf;
	}

	len = strlen(filename);
	strncpy(namevirt, filename, namesize);

	if (copy_from_user(datavirt, buf, count)) {
		ret = -EFAULT;
		goto free_databuf;
	}

	ret = mrq_debugfs_write(bpmp, namephys, len, dataphys,
				count);

free_databuf:
	dma_free_coherent(bpmp->dev, datasize, datavirt, dataphys);
free_namebuf:
	dma_free_coherent(bpmp->dev, namesize, namevirt, namephys);

	return ret ?: count;
}

static const struct file_operations debugfs_fops = {
	.open		= debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= debugfs_store,
	.release	= single_release,
};

static int bpmp_populate_dir(struct tegra_bpmp *bpmp, struct seqbuf *seqbuf,
			     struct dentry *parent, uint32_t depth)
{
	int err;
	uint32_t d, t;
	const char *name;
	struct dentry *dentry;

	while (!seqbuf_eof(seqbuf)) {
		err = seqbuf_read_u32(seqbuf, &d);
		if (err < 0)
			return err;

		if (d < depth) {
			seqbuf_seek(seqbuf, -4);
			/* go up a level */
			return 0;
		} else if (d != depth) {
			/* malformed data received from BPMP */
			return -EIO;
		}

		err = seqbuf_read_u32(seqbuf, &t);
		if (err < 0)
			return err;
		err = seqbuf_read_str(seqbuf, &name);
		if (err < 0)
			return err;

		if (t & DEBUGFS_S_ISDIR) {
			dentry = debugfs_create_dir(name, parent);
			if (!dentry)
				return -ENOMEM;
			err = bpmp_populate_dir(bpmp, seqbuf, dentry, depth+1);
			if (err < 0)
				return err;
		} else {
			umode_t mode;

			mode = t & DEBUGFS_S_IRUSR ? S_IRUSR : 0;
			mode |= t & DEBUGFS_S_IWUSR ? S_IWUSR : 0;
			dentry = debugfs_create_file(name, mode,
						     parent, bpmp,
						     &debugfs_fops);
			if (!dentry)
				return -ENOMEM;
		}
	}

	return 0;
}

static int create_debugfs_mirror(struct tegra_bpmp *bpmp, void *buf,
				 size_t bufsize, struct dentry *root)
{
	struct seqbuf seqbuf;
	int err;

	bpmp->debugfs_mirror = debugfs_create_dir("debug", root);
	if (!bpmp->debugfs_mirror)
		return -ENOMEM;

	seqbuf_init(&seqbuf, buf, bufsize);
	err = bpmp_populate_dir(bpmp, &seqbuf, bpmp->debugfs_mirror, 0);
	if (err < 0) {
		debugfs_remove_recursive(bpmp->debugfs_mirror);
		bpmp->debugfs_mirror = NULL;
	}

	return err;
}

int tegra_bpmp_init_debugfs(struct tegra_bpmp *bpmp)
{
	dma_addr_t phys;
	void *virt;
	const size_t sz = SZ_256K;
	size_t nbytes;
	int ret;
	struct dentry *root;

	if (!tegra_bpmp_mrq_is_supported(bpmp, MRQ_DEBUGFS))
		return 0;

	root = debugfs_create_dir("bpmp", NULL);
	if (!root)
		return -ENOMEM;

	virt = dma_alloc_coherent(bpmp->dev, sz, &phys,
				  GFP_KERNEL | GFP_DMA32);
	if (!virt) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mrq_debugfs_dumpdir(bpmp, phys, sz, &nbytes);
	if (ret < 0)
		goto free;

	ret = create_debugfs_mirror(bpmp, virt, nbytes, root);
free:
	dma_free_coherent(bpmp->dev, sz, virt, phys);
out:
	if (ret < 0)
		debugfs_remove(root);

	return ret;
}
