// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/bpf_bio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jump_label.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <linux/blk-mq.h>

/* Global BPF bio ops - shared with bio.c */
DEFINE_STATIC_KEY_FALSE(bpf_bio_ops_enabled);
EXPORT_SYMBOL_GPL(bpf_bio_ops_enabled);

const struct bpf_bio_ops *bpf_bio_ops __read_mostly;
EXPORT_SYMBOL_GPL(bpf_bio_ops);

/* BPF struct_ops callbacks */
static int bpf_bio_submit_bio(struct bio *bio)
{
    return 0; /* Continue with normal submission */
}

static void bpf_bio_endio(struct bio *bio)
{
    /* Just a hook, no default action needed */
}

/* Helper function implementations */
void bpf_bio_get_context(struct bio *bio, struct bpf_bio_context *ctx)
{
    if (!bio || !ctx)
        return;

    ctx->sector = bio->bi_iter.bi_sector;
    ctx->size = bio->bi_iter.bi_size;
    ctx->op = bio_op(bio);
    ctx->flags = bio->bi_opf & ~REQ_OP_MASK;
    ctx->dev_id = bio->bi_bdev ? MINOR(bio->bi_bdev->bd_dev) : 0;
}
EXPORT_SYMBOL_GPL(bpf_bio_get_context);

void bpf_bio_set_status(struct bio *bio, int status)
{
    if (!bio)
        return;

    bio->bi_status = blk_status_to_errno(status);
}
EXPORT_SYMBOL_GPL(bpf_bio_set_status);

void bpf_bio_resubmit(struct bio *bio)
{
    if (!bio)
        return;

    /* Resubmit the bio through the normal path */
    submit_bio_noacct(bio);
}
EXPORT_SYMBOL_GPL(bpf_bio_resubmit);

/* Default BPF bio ops */
static const struct bpf_bio_ops default_bpf_bio_ops = {
    .submit_bio_hook = bpf_bio_submit_bio,
    .bio_endio_hook = bpf_bio_endio,
};

/* Initialize BPF bio ops */
int bpf_bio_ops_init(void)
{
    /* Set default ops */
    bpf_bio_ops = &default_bpf_bio_ops;
    return 0;
}

void bpf_bio_ops_unregister(void)
{
    bpf_bio_ops = NULL;
}

/* Make sure bio hooks are initialized early */
static int __init bpf_bio_hooks_init(void)
{
    return bpf_bio_ops_init();
}
subsys_initcall(bpf_bio_hooks_init); 