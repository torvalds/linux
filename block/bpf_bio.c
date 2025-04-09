// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/bpf_bio.h>
#include <linux/btf.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jump_label.h>

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

/* Default BPF bio ops */
static const struct bpf_bio_ops default_bpf_bio_ops = {
    .submit_bio_hook = bpf_bio_submit_bio,
    .bio_endio_hook = bpf_bio_endio,
};

/* Register/unregister callbacks */
static int bpf_bio_ops_reg(void)
{
    static_branch_enable(&bpf_bio_ops_enabled);
    return 0;
}

static void bpf_bio_ops_unreg(void)
{
    static_branch_disable(&bpf_bio_ops_enabled);
}

/* Register BPF struct_ops */
BTF_ID_LIST_SINGLE(bpf_bio_ops_btf_ids, struct, bpf_bio_ops)

static const struct bpf_struct_ops bpf_bio_ops_def = {
    .name = "bpf_bio_ops",
    .type_id = BPF_BIO_OPS_TYPE_ID,
    .init = NULL, /* No special initialization needed */
    .reg = bpf_bio_ops_reg,
    .unreg = bpf_bio_ops_unreg,
    .check_member = NULL, /* Use default checker */
    .btf_id = &bpf_bio_ops_btf_ids[0],
};

/* Initialize BPF bio ops */
int bpf_bio_ops_init(void)
{
    int ret;

    ret = register_bpf_struct_ops(&bpf_bio_ops_def);
    if (ret < 0)
        return ret;

    /* Set default ops */
    bpf_bio_ops = &default_bpf_bio_ops;
    return 0;
}

void bpf_bio_ops_unregister(void)
{
    bpf_bio_ops = NULL;
} 