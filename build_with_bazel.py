#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.

import argparse
import errno
import glob
import logging
import os
import re
import sys
import subprocess

HOST_TARGETS = ["dtc"]
DEFAULT_SKIP_LIST = ["abi", "test_mapping"]
MSM_EXTENSIONS = "build/msm_kernel_extensions.bzl"
ABL_EXTENSIONS = "build/abl_extensions.bzl"
DEFAULT_MSM_EXTENSIONS_SRC = "../msm-kernel/msm_kernel_extensions.bzl"
DEFAULT_ABL_EXTENSIONS_SRC = "../bootable/bootloader/edk2/abl_extensions.bzl"


class BazelBuilder:
    """Helper class for building with Bazel"""

    def __init__(self, target_list, skip_list, out_dir, user_opts):
        self.workspace = os.path.realpath(
            os.path.join(os.path.dirname(os.path.realpath(__file__)), "..")
        )
        self.bazel_bin = os.path.join(self.workspace, "tools", "bazel")
        if not os.path.exists(self.bazel_bin):
            logging.error("failed to find Bazel binary at %s", self.bazel_bin)
            sys.exit(1)
        self.kernel_dir = os.path.basename(
            (os.path.dirname(os.path.realpath(__file__)))
        )

        for t, v in target_list:
            if not t or not v:
                logging.error("invalid target_variant combo \"%s_%s\"", t, v)
                sys.exit(1)

        self.target_list = target_list
        self.skip_list = skip_list
        self.out_dir = out_dir
        self.user_opts = user_opts
        self.process_list = []
        self.setup_extensions()

    def __del__(self):
        for proc in self.process_list:
            try:
                proc.kill()
                proc.wait()
            except OSError:
                pass

    def setup_extensions(self):
        """Set up the extension files if needed"""
        for (ext, def_src) in [
            (MSM_EXTENSIONS, DEFAULT_MSM_EXTENSIONS_SRC),
            (ABL_EXTENSIONS, DEFAULT_ABL_EXTENSIONS_SRC),
        ]:
            ext_path = os.path.join(self.workspace, ext)
            # If the file doesn't exist or is a dead link, link to the default
            try:
                os.stat(ext_path)
            except OSError as e:
                if e.errno == errno.ENOENT:
                    logging.info(
                        "%s does not exist or is a broken symlink... linking to default at %s",
                        ext,
                        def_src,
                    )
                    if os.path.islink(ext_path):
                        os.unlink(ext_path)
                    os.symlink(def_src, ext_path)
                else:
                    raise e

    def get_build_targets(self):
        """Query for build targets"""
        logging.info("Querying build targets...")

        cross_target_list = []
        host_target_list = []
        for t, v in self.target_list:
            if v == "ALL":
                skip_list_re = [
                    re.compile(r"//{}:{}_.*_{}_dist".format(self.kernel_dir, t, s))
                    for s in self.skip_list
                ]
                host_target_list_re = [
                    re.compile(r"//{}:{}_.*_{}_dist".format(self.kernel_dir, t, h))
                    for h in HOST_TARGETS
                ]
                query = 'filter("{}_.*_dist$", attr(generator_function, define_msm_platforms, {}/...))'.format(
                    t, self.kernel_dir
                )
            else:
                skip_list_re = [
                    re.compile(r"//{}:{}_{}_{}_dist".format(self.kernel_dir, t, v, s))
                    for s in self.skip_list
                ]
                host_target_list_re = [
                    re.compile(r"//{}:{}_{}_{}_dist".format(self.kernel_dir, t, v, h))
                    for h in HOST_TARGETS
                ]
                query = 'filter("{}_{}.*_dist$", attr(generator_function, define_msm_platforms, {}/...))'.format(
                    t, v, self.kernel_dir
                )

            cmdline = [
                self.bazel_bin,
                "query",
                "--ui_event_filters=-info",
                "--noshow_progress",
                query,
            ]

            logging.debug('Running "%s"', " ".join(cmdline))

            try:
                query_cmd = subprocess.Popen(
                    cmdline, cwd=self.workspace, stdout=subprocess.PIPE
                )
                self.process_list.append(query_cmd)
                target_list = [l.decode("utf-8") for l in query_cmd.stdout.read().splitlines()]
            except Exception as e:
                logging.error(e)
                sys.exit(1)

            self.process_list.remove(query_cmd)

            if not target_list:
                logging.error(
                    "failed to find any Bazel targets for target/variant combo %s_%s",
                    t,
                    v,
                )
                sys.exit(1)

            for target in target_list:
                if any((skip_re.match(target) for skip_re in skip_list_re)):
                    continue
                if any((host_re.match(target) for host_re in host_target_list_re)):
                    host_target_list.append(target)
                else:
                    cross_target_list.append(target)

        # Sort build targets by string length to guarantee the base target goes
        # first when copying to output directory
        cross_target_list.sort(key=len)
        host_target_list.sort(key=len)

        return (cross_target_list, host_target_list)

    def clean_legacy_generated_files(self):
        """Clean generated files from legacy build to avoid conflicts with Bazel"""
        for f in glob.glob("{}/msm-kernel/arch/arm64/configs/vendor/*_defconfig".format(self.workspace)):
            os.remove(f)

        f = os.path.join(self.workspace, "bootable", "bootloader", "edk2", "Conf", ".AutoGenIdFile.txt")
        if os.path.exists(f):
            os.remove(f)

        for root, _, files in os.walk(os.path.join(self.workspace, "bootable")):
            for f in files:
                if f.endswith(".pyc"):
                    os.remove(os.path.join(root, f))

    def bazel(
        self,
        bazel_subcommand,
        targets,
        extra_options=None,
        bazel_target_opts=None,
    ):
        """Execute a bazel command"""
        cmdline = [self.bazel_bin, bazel_subcommand]
        if extra_options:
            cmdline.extend(extra_options)
        cmdline.extend(targets)
        if bazel_target_opts is not None:
            cmdline.extend(["--"] + bazel_target_opts)

        cmdline_str = " ".join(cmdline)
        try:
            logging.info('Running "%s"', cmdline_str)
            build_proc = subprocess.Popen(cmdline_str, cwd=self.workspace, shell=True)
            self.process_list.append(build_proc)
            build_proc.wait()
            if build_proc.returncode != 0:
                sys.exit(build_proc.returncode)
        except Exception as e:
            logging.error(e)
            sys.exit(1)

        self.process_list.remove(build_proc)

    def build_targets(self, targets, user_opts=None):
        """Run "bazel build" on all targets in parallel"""
        if not targets:
            logging.warning("no targets to build")
        self.bazel("build", targets, extra_options=user_opts)

    def run_targets(self, targets, out_subdir="dist", user_opts=None):
        """Run "bazel run" on all targets in serial (since bazel run cannot have multiple targets)"""
        bto = []
        if self.out_dir:
            bto.extend(["--dist_dir", os.path.join(self.out_dir, out_subdir)])
        for target in targets:
            self.bazel("run", [target], extra_options=user_opts, bazel_target_opts=bto)

    def run_menuconfig(self):
        """Run menuconfig on all target-variant combos class is initialized with"""
        for t, v in self.target_list:
            self.bazel("run", ["//{}:{}_{}_config".format(self.kernel_dir, t, v)],
                    bazel_target_opts=["menuconfig"])

    def build(self):
        """Determine which targets to build, then build them"""
        cross_targets_to_build, host_targets_to_build = self.get_build_targets()

        logging.debug(
            "Building the following device targets:\n%s",
            "\n".join(cross_targets_to_build),
        )
        logging.debug(
            "Building the following host targets:\n%s", "\n".join(host_targets_to_build)
        )

        if not cross_targets_to_build and not host_targets_to_build:
            logging.error("no targets to build")
            sys.exit(1)

        self.clean_legacy_generated_files()

        if self.skip_list:
            self.user_opts.extend(["--//msm-kernel:skip_{}=true".format(s) for s in self.skip_list])

        self.user_opts.append("--config=stamp")
        device_user_opts = self.user_opts + ["--config=android_arm64"]

        logging.info("Building device targets...")
        self.build_targets(
            cross_targets_to_build,
            user_opts=device_user_opts,
        )
        self.run_targets(
            cross_targets_to_build,
            user_opts=device_user_opts,
        )

        logging.info("Building host targets...")
        self.build_targets(
            host_targets_to_build, user_opts=self.user_opts
        )
        self.run_targets(
            host_targets_to_build, out_subdir="host", user_opts=self.user_opts
        )


def main():
    """Main script entrypoint"""
    parser = argparse.ArgumentParser(description="Build kernel platform with Bazel")

    parser.add_argument(
        "-t",
        "--target",
        metavar=("TARGET", "VARIANT"),
        action="append",
        nargs=2,
        required=True,
        help='Target and variant to build (e.g. -t kalama gki). May be passed multiple times. A special VARIANT may be passed, "ALL", which will build all variants for a particular target',
    )
    parser.add_argument(
        "-s",
        "--skip",
        metavar="BUILD_RULE",
        action="append",
        default=[],
        help="Skip specific build rules (e.g. --skip abl will skip the //msm-kernel:<target>_<variant>_abl build)",
    )
    parser.add_argument(
        "-o",
        "--out_dir",
        metavar="OUT_DIR",
        help='Specify the output distribution directory (by default, "$PWD/out/msm-kernel-<target>-variant")',
    )
    parser.add_argument(
        "--log",
        metavar="LEVEL",
        default="info",
        choices=["debug", "info", "warning", "error"],
        help="Log level (debug, info, warning, error)",
    )
    parser.add_argument(
        "-c",
        "--menuconfig",
        action="store_true",
        help="Run menuconfig for <target>-<variant> and exit without building",
    )

    args, user_opts = parser.parse_known_args(sys.argv[1:])

    logging.basicConfig(
        level=getattr(logging, args.log.upper()),
        format="[{}] %(levelname)s: %(message)s".format(os.path.basename(sys.argv[0])),
    )

    args.skip.extend(DEFAULT_SKIP_LIST)

    builder = BazelBuilder(args.target, args.skip, args.out_dir, user_opts)
    try:
        if args.menuconfig:
            builder.run_menuconfig()
        else:
            builder.build()
    except KeyboardInterrupt:
        logging.info("Received keyboard interrupt... exiting")
        del builder
        sys.exit(1)

    logging.info("Build completed successfully!")


if __name__ == "__main__":
    main()
