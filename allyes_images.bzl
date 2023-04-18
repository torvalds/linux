def gen_allyes_files(le_target):
    """"Build empty vendor_boot/init_boot/super images fr allyes config."""
    rule_name = "{}_dummy_files".format(le_target)
    native.genrule(
        name = rule_name,
        srcs = [],
        outs = ["vendor_boot.img", "super.img", "init_boot.img"],
        cmd = """touch $(OUTS)
                 echo 'empty_file' | tee $(OUTS)"""
    )

