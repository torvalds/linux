def sign_boot_img(ctx):
    inputs = []
    inputs += ctx.files.artifacts
    inputs += ctx.files.avbtool
    inputs += ctx.files.key

    outputs = ctx.actions.declare_file("{}/boot.img".format(ctx.label.name))

    for artifact in ctx.files.artifacts:
        if artifact.basename == "boot.img":
            boot_img = artifact
            break

    if not boot_img:
        fail("artifacts must include file named \"boot.img\"")

    proplist = " ".join(["--prop {}".format(x) for x in ctx.attr.props])

    command = """
    cp {boot_img} {boot_dir}/{boot_name}
    {tool} add_hash_footer --image {boot_dir}/{boot_name} --algorithm SHA256_RSA4096 \
            --key {key} --partition_size {boot_partition_size} --partition_name boot \
            {proplist}
    """.format(
        boot_img = boot_img.path,
        tool = ctx.file.avbtool.path,
        key = ctx.file.key.path,
        boot_dir = outputs.dirname,
        boot_name = outputs.basename,
        boot_partition_size = ctx.attr.boot_partition_size,
        proplist = proplist,
    )

    ctx.actions.run_shell(
        mnemonic = "SignBootImg",
        inputs = inputs,
        outputs = [outputs],
        command = command,
        progress_message = "Signing boot image from artifacts",
    )

    return [
        DefaultInfo(
            files = depset([outputs]),
        ),
    ]

avb_sign_boot_image = rule(
    implementation = sign_boot_img,
    doc = "Sign the boot image present in artifacts",
    attrs = {
        "artifacts": attr.label(
            mandatory = True,
            allow_files = True,
        ),
        "avbtool": attr.label(
            mandatory = True,
            allow_single_file = True,
        ),
        "key": attr.label(
            mandatory = True,
            allow_single_file = True,
        ),
        "boot_partition_size": attr.int(
            mandatory = False,
            default = 0x6000000,  # bytes, = 98304 kb
            doc = "Final size of boot.img desired",
        ),
        "props": attr.string_list(
            mandatory = True,
            allow_empty = False,
            doc = "List of key:value pairs",
        ),
    },
)
