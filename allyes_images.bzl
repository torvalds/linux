load(":avb_boot_img.bzl", "avb_sign_boot_image")

def gen_allyes_files(le_target, msm_dist_targets):
    """"Build empty vendor_boot/init_boot/super images fr allyes config."""
    rule_name = "{}_dummy_files".format(le_target)
    native.genrule(
        name = rule_name,
        srcs = [],
        outs = ["vendor_boot.img", "super.img", "init_boot.img"],
        cmd = """touch $(OUTS)
                 echo 'empty_file' | tee $(OUTS)""",
    )
    avb_sign_boot_image(
        name = "{}_avb_sign_boot_image".format(le_target),
        artifacts = "{}_images".format(le_target),
        avbtool = "//prebuilts/kernel-build-tools:linux-x86/bin/avbtool",
        key = "//tools/mkbootimg:gki/testdata/testkey_rsa4096.pem",
        props = [
            "com.android.build.boot.os_version:13",
            "com.android.build.boot.security_patch:2023-05-05",
        ],
        boot_partition_size = 0x6000000,
    )

    msm_dist_targets.append("{}_avb_sign_boot_image".format(le_target))
