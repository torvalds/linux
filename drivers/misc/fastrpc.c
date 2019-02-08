// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2018, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/rpmsg.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#define ADSP_DOMAIN_ID (0)
#define MDSP_DOMAIN_ID (1)
#define SDSP_DOMAIN_ID (2)
#define CDSP_DOMAIN_ID (3)
#define FASTRPC_DEV_MAX		4 /* adsp, mdsp, slpi, cdsp*/
#define FASTRPC_MAX_SESSIONS	9 /*8 compute, 1 cpz*/
#define FASTRPC_CTX_MAX (256)
#define FASTRPC_CTXID_MASK (0xFF0)
#define FASTRPC_DEVICE_NAME	"fastrpc"

#define miscdev_to_cctx(d) container_of(d, struct fastrpc_channel_ctx, miscdev)

static const char *domains[FASTRPC_DEV_MAX] = { "adsp", "mdsp",
						"sdsp", "cdsp"};

struct fastrpc_session_ctx {
	struct device *dev;
	int sid;
	bool used;
	bool valid;
};

struct fastrpc_channel_ctx {
	int domain_id;
	int sesscount;
	struct rpmsg_device *rpdev;
	struct fastrpc_session_ctx session[FASTRPC_MAX_SESSIONS];
	spinlock_t lock;
	struct idr ctx_idr;
	struct list_head users;
	struct miscdevice miscdev;
};

struct fastrpc_user {
	struct list_head user;
	struct list_head maps;
	struct list_head pending;

	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_session_ctx *sctx;

	int tgid;
	int pd;
	/* Lock for lists */
	spinlock_t lock;
	/* lock for allocations */
	struct mutex mutex;
};

static struct fastrpc_session_ctx *fastrpc_session_alloc(
					struct fastrpc_channel_ctx *cctx)
{
	struct fastrpc_session_ctx *session = NULL;
	int i;

	spin_lock(&cctx->lock);
	for (i = 0; i < cctx->sesscount; i++) {
		if (!cctx->session[i].used && cctx->session[i].valid) {
			cctx->session[i].used = true;
			session = &cctx->session[i];
			break;
		}
	}
	spin_unlock(&cctx->lock);

	return session;
}

static void fastrpc_session_free(struct fastrpc_channel_ctx *cctx,
				 struct fastrpc_session_ctx *session)
{
	spin_lock(&cctx->lock);
	session->used = false;
	spin_unlock(&cctx->lock);
}

static int fastrpc_device_release(struct inode *inode, struct file *file)
{
	struct fastrpc_user *fl = (struct fastrpc_user *)file->private_data;
	struct fastrpc_channel_ctx *cctx = fl->cctx;

	spin_lock(&cctx->lock);
	list_del(&fl->user);
	spin_unlock(&cctx->lock);

	fastrpc_session_free(cctx, fl->sctx);

	mutex_destroy(&fl->mutex);
	kfree(fl);
	file->private_data = NULL;

	return 0;
}

static int fastrpc_device_open(struct inode *inode, struct file *filp)
{
	struct fastrpc_channel_ctx *cctx = miscdev_to_cctx(filp->private_data);
	struct fastrpc_user *fl = NULL;

	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl)
		return -ENOMEM;

	filp->private_data = fl;
	spin_lock_init(&fl->lock);
	mutex_init(&fl->mutex);
	INIT_LIST_HEAD(&fl->pending);
	INIT_LIST_HEAD(&fl->maps);
	INIT_LIST_HEAD(&fl->user);
	fl->tgid = current->tgid;
	fl->cctx = cctx;
	spin_lock(&cctx->lock);
	list_add_tail(&fl->user, &cctx->users);
	spin_unlock(&cctx->lock);
	fl->sctx = fastrpc_session_alloc(cctx);

	return 0;
}

static const struct file_operations fastrpc_fops = {
	.open = fastrpc_device_open,
	.release = fastrpc_device_release,
};

static int fastrpc_cb_probe(struct platform_device *pdev)
{
	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_session_ctx *sess;
	struct device *dev = &pdev->dev;
	int i, sessions = 0;

	cctx = dev_get_drvdata(dev->parent);
	if (!cctx)
		return -EINVAL;

	of_property_read_u32(dev->of_node, "qcom,nsessions", &sessions);

	spin_lock(&cctx->lock);
	sess = &cctx->session[cctx->sesscount];
	sess->used = false;
	sess->valid = true;
	sess->dev = dev;
	dev_set_drvdata(dev, sess);

	if (of_property_read_u32(dev->of_node, "reg", &sess->sid))
		dev_info(dev, "FastRPC Session ID not specified in DT\n");

	if (sessions > 0) {
		struct fastrpc_session_ctx *dup_sess;

		for (i = 1; i < sessions; i++) {
			if (cctx->sesscount++ >= FASTRPC_MAX_SESSIONS)
				break;
			dup_sess = &cctx->session[cctx->sesscount];
			memcpy(dup_sess, sess, sizeof(*dup_sess));
		}
	}
	cctx->sesscount++;
	spin_unlock(&cctx->lock);
	dma_set_mask(dev, DMA_BIT_MASK(32));

	return 0;
}

static int fastrpc_cb_remove(struct platform_device *pdev)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(pdev->dev.parent);
	struct fastrpc_session_ctx *sess = dev_get_drvdata(&pdev->dev);
	int i;

	spin_lock(&cctx->lock);
	for (i = 1; i < FASTRPC_MAX_SESSIONS; i++) {
		if (cctx->session[i].sid == sess->sid) {
			cctx->session[i].valid = false;
			cctx->sesscount--;
		}
	}
	spin_unlock(&cctx->lock);

	return 0;
}

static const struct of_device_id fastrpc_match_table[] = {
	{ .compatible = "qcom,fastrpc-compute-cb", },
	{}
};

static struct platform_driver fastrpc_cb_driver = {
	.probe = fastrpc_cb_probe,
	.remove = fastrpc_cb_remove,
	.driver = {
		.name = "qcom,fastrpc-cb",
		.of_match_table = fastrpc_match_table,
		.suppress_bind_attrs = true,
	},
};

static int fastrpc_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct device *rdev = &rpdev->dev;
	struct fastrpc_channel_ctx *data;
	int i, err, domain_id = -1;
	const char *domain;

	data = devm_kzalloc(rdev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	err = of_property_read_string(rdev->of_node, "label", &domain);
	if (err) {
		dev_info(rdev, "FastRPC Domain not specified in DT\n");
		return err;
	}

	for (i = 0; i <= CDSP_DOMAIN_ID; i++) {
		if (!strcmp(domains[i], domain)) {
			domain_id = i;
			break;
		}
	}

	if (domain_id < 0) {
		dev_info(rdev, "FastRPC Invalid Domain ID %d\n", domain_id);
		return -EINVAL;
	}

	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = kasprintf(GFP_KERNEL, "fastrpc-%s",
				domains[domain_id]);
	data->miscdev.fops = &fastrpc_fops;
	err = misc_register(&data->miscdev);
	if (err)
		return err;

	dev_set_drvdata(&rpdev->dev, data);
	dma_set_mask_and_coherent(rdev, DMA_BIT_MASK(32));
	INIT_LIST_HEAD(&data->users);
	spin_lock_init(&data->lock);
	idr_init(&data->ctx_idr);
	data->domain_id = domain_id;
	data->rpdev = rpdev;

	return of_platform_populate(rdev->of_node, NULL, NULL, rdev);
}

static void fastrpc_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(&rpdev->dev);

	misc_deregister(&cctx->miscdev);
	of_platform_depopulate(&rpdev->dev);
	kfree(cctx);
}

static int fastrpc_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				  int len, void *priv, u32 addr)
{
	return 0;
}

static const struct of_device_id fastrpc_rpmsg_of_match[] = {
	{ .compatible = "qcom,fastrpc" },
	{ },
};
MODULE_DEVICE_TABLE(of, fastrpc_rpmsg_of_match);

static struct rpmsg_driver fastrpc_driver = {
	.probe = fastrpc_rpmsg_probe,
	.remove = fastrpc_rpmsg_remove,
	.callback = fastrpc_rpmsg_callback,
	.drv = {
		.name = "qcom,fastrpc",
		.of_match_table = fastrpc_rpmsg_of_match,
	},
};

static int fastrpc_init(void)
{
	int ret;

	ret = platform_driver_register(&fastrpc_cb_driver);
	if (ret < 0) {
		pr_err("fastrpc: failed to register cb driver\n");
		return ret;
	}

	ret = register_rpmsg_driver(&fastrpc_driver);
	if (ret < 0) {
		pr_err("fastrpc: failed to register rpmsg driver\n");
		platform_driver_unregister(&fastrpc_cb_driver);
		return ret;
	}

	return 0;
}
module_init(fastrpc_init);

static void fastrpc_exit(void)
{
	platform_driver_unregister(&fastrpc_cb_driver);
	unregister_rpmsg_driver(&fastrpc_driver);
}
module_exit(fastrpc_exit);

MODULE_LICENSE("GPL v2");
