#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

import argparse
import logging
import os
import sys
from subprocess import Popen

BAZEL_BIN = "./tools/bazel"
CPU = "aarch64"
HOST_CROSSTOOL = "@bazel_tools//tools/cpp:toolchain"


def get_cross_cli_opts(toolchain):
    return [
        "--cpu={}".format(CPU),
        "--crosstool_top={}".format(toolchain),
        "--host_crosstool_top={}".format(HOST_CROSSTOOL),
    ]


def bazel(bazel_subcommand, targets, extra_options=[], us_cross_toolchain=None):
    cmdline = [BAZEL_BIN, bazel_subcommand]
    cmdline.extend(extra_options)
    if us_cross_toolchain:
        cmdline.extend(get_cross_cli_opts(us_cross_toolchain))
    cmdline.extend(targets)
    cmdline_str = " ".join(cmdline)
    try:
        logging.debug('Running "%s"', cmdline_str)
        build_proc = Popen(cmdline_str, shell=True)
        build_proc.wait()
        if build_proc.returncode != 0:
            sys.exit(build_proc.returncode)
    except Exception as e:
        logging.error(e),
        sys.exit(1)


def build_targets(targets, user_opts=[], us_cross_toolchain=None):
    if not targets:
        logging.warning("no targets to build")
    bazel("build", targets, user_opts, us_cross_toolchain)


def run_targets(targets, user_opts=[], us_cross_toolchain=None):
    # bazel cannot _run_ multiple targets at once, so run them in serial
    for target in targets:
        bazel("run", [target], user_opts, us_cross_toolchain)


def build(
    target_list,
    user_opts=[],
    us_cross_toolchain=None,
    extra_targets=[],
    skip_kernel=False,
    skip_abl=False,
    skip_host_tools=False,
    **ignored
):
    msm_targets = ["//msm-kernel:{}_{}".format(t, v) for t, v in target_list]
    extra_msm_targets = []
    for extra in extra_targets:
        extra_msm_targets.extend(
            ["//msm-kernel:{}_{}_{}".format(t, v, extra) for t, v in target_list]
        )

    targets_to_build = []
    host_targets_to_build = []
    for t in msm_targets:
        logging.info(
            "skipping kernel build"
        ) if skip_kernel else targets_to_build.append(t)
        logging.info("skipping ABL build") if skip_abl else targets_to_build.append(
            t + "_abl"
        )
        logging.info(
            "skipping host tools build"
        ) if skip_host_tools else host_targets_to_build.append(t + "_dtc")

    targets_to_build.append(t + "_test_mapping")

    targets_to_build += extra_msm_targets

    if not targets_to_build:
        logging.error("no targets to build")
        sys.exit(1)

    logging.info("Building {} targets...".format(CPU))
    build_targets(targets_to_build, user_opts, us_cross_toolchain)
    run_targets([t + "_dist" for t in targets_to_build], user_opts, us_cross_toolchain)

    logging.info("Building host targets...")
    build_targets(host_targets_to_build, user_opts)
    run_targets([t + "_dist" for t in host_targets_to_build], user_opts)


def main():
    parser = argparse.ArgumentParser(description="Build kernel platform with Bazel")

    parser.add_argument(
        "-t",
        "--target",
        metavar=("TARGET", "VARIANT"),
        action="append",
        nargs=2,
        required=True,
        help="Target and variant to build (e.g. -t kalama gki). May be passed multiple times.",
    )
    parser.add_argument(
        "-u",
        "--userspace_cross_toolchain",
        default=None,
        metavar="TOOLCHAIN",
        help="Set a Bazel toolchain for cross-compiled userspace programs",
    )
    parser.add_argument(
        "-e",
        "--extra_build_target",
        metavar="BUILD_TARGET",
        action="append",
        default=[],
        help="""
           Additional Bazel target to build and run dist. For example, "-t kalama gki --extra_target foo"
           would run "bazel build //msm-kernel:kalama_gki_foo && bazel run //msms-kernel:kalama_gki_foo_dist".
           May be passed multiple times.
        """,
    )
    parser.add_argument(
        "--skip_kernel",
        action="store_true",
        help="Skip building the kernel (note: may still need to build kernel if building ABL or tests)",
    )
    parser.add_argument(
        "--skip_abl",
        action="store_true",
        help="Skip building the Android Boot Loader (ABL)",
    )
    parser.add_argument(
        "--skip_host_tools",
        action="store_true",
        help="Skip building host tools (DTC, etc.)",
    )
    parser.add_argument(
        "--log",
        help="Log level (debug, info, warning, error)",
        default="debug",
        choices=["debug", "info", "warning", "error"],
    )

    args, user_opts = parser.parse_known_args(sys.argv[1:])

    logging.basicConfig(
        level=getattr(logging, args.log.upper()),
        format="[{}] %(levelname)s: %(message)s".format(os.path.basename(sys.argv[0])),
    )

    extra_targets = list(args.extra_build_target)

    build(
        args.target,
        user_opts,
        args.userspace_cross_toolchain,
        extra_targets,
        **vars(args)
    )

    logging.info("Build completed successfully!")


if __name__ == "__main__":
    main()
