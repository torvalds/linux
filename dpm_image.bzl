load("@//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_genrule")

def define_dpm_image(tv):
    target = tv.split("_")[0]
    hermetic_genrule(
        name = "{}_dpm_image".format(tv),
        srcs = [
            "//msm-kernel:{}_build_config".format(tv),
            "//msm-kernel:{}/{}-dpm-overlay.dtbo".format(tv, target),
        ],
        outs = ["{}/dpm.img".format(tv)],
        cmd = """
            # Stub out append_cmd
            append_cmd() {{
              :
            }}

            set +u
            source "$(location //msm-kernel:{tv}_build_config)"
            set -u

            $(location //prebuilts/kernel-build-tools:linux-x86/bin/mkdtboimg) \
                    create "$@" --page_size="$$PAGE_SIZE" \
                    "$(location //msm-kernel:{tv}/{target}-dpm-overlay.dtbo)"
        """.format(
            tv = tv,
            target = target,
        ),
        tools = [
            "//prebuilts/kernel-build-tools:linux-x86/bin/mkdtboimg",
        ],
    )
