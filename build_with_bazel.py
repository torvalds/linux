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
DEFAULT_OUT_DIR = "{workspace}/out/msm-kernel-{target}-{variant}"

class Target:
    def __init__(self, workspace, target, variant, bazel_label, out_dir=None):
        self.workspace = workspace
        self.target = target
        self.variant = variant
        self.bazel_label = bazel_label
        self.out_dir = out_dir

    def __lt__(self, other):
        return len(self.bazel_label) < len(other.bazel_label)

    def get_out_dir(self, suffix=None):
        if self.out_dir:
            out_dir = self.out_dir
        else:
            # Mirror the logic in msm_common.bzl:get_out_dir()
            if "allyes" in self.target:
                target_norm = self.target.replace("_", "-")
            else:
                target_norm = self.target.replace("-", "_")

            variant_norm = self.variant.replace("-", "_")

            out_dir = DEFAULT_OUT_DIR.format(
                workspace = self.workspace, target=target_norm, variant=variant_norm
            )

        if suffix:
            return os.path.join(out_dir, suffix)
        else:
            return out_dir

class BazelBuilder:
    """Helper class for building with Bazel"""

    def __init__(self, target_list, skip_list, out_dir, dry_run, user_opts):
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
        self.dry_run = dry_run
        self.user_opts = user_opts
        self.process_list = []
        if len(self.target_list) > 1 and out_dir:
            logging.error("cannot specify multiple targets with one out dir")
            sys.exit(1)
        else:
            self.out_dir = out_dir

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
                if self.out_dir:
                    logging.error("cannot specify multiple targets (ALL variants) with one out dir")
                    sys.exit(1)

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
                label_list = [l.decode("utf-8") for l in query_cmd.stdout.read().splitlines()]
            except Exception as e:
                logging.error(e)
                sys.exit(1)

            self.process_list.remove(query_cmd)

            if not label_list:
                logging.error(
                    "failed to find any Bazel targets for target/variant combo %s_%s",
                    t,
                    v,
                )
                sys.exit(1)

            for label in label_list:
                if any((skip_re.match(label) for skip_re in skip_list_re)):
                    continue

                if v == "ALL":
                    real_variant = re.search(
                        r"//{}:{}_([^_]+)_".format(self.kernel_dir, t), label
                    ).group(1)
                else:
                    real_variant = v

                if any((host_re.match(label) for host_re in host_target_list_re)):
                    host_target_list.append(
                        Target(self.workspace, t, real_variant, label, self.out_dir)
                    )
                else:
                    cross_target_list.append(
                        Target(self.workspace, t, real_variant, label, self.out_dir)
                    )

        # Sort build targets by label string length to guarantee the base target goes
        # first when copying to output directory
        cross_target_list.sort()
        host_target_list.sort()

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
        cmdline.extend([t.bazel_label for t in targets])
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
        for target in targets:
            out_dir = target.get_out_dir(out_subdir)
            self.bazel("run", [target], extra_options=user_opts, bazel_target_opts=["--dist_dir", out_dir])
            self.write_opts(out_dir, user_opts)

    def run_menuconfig(self):
        """Run menuconfig on all target-variant combos class is initialized with"""
        for t, v in self.target_list:
            menuconfig_label = "//{}:{}_{}_config".format(self.kernel_dir, t, v)
            menuconfig_target = [Target(self.workspace, t, v, menuconfig_label, self.out_dir)]
            self.bazel("run", menuconfig_target, bazel_target_opts=["menuconfig"])

    def write_opts(self, out_dir, user_opts=None):
        with open(os.path.join(out_dir, "build_opts.txt"), "w") as opt_file:
            if user_opts:
                opt_file.write("{}".format("\n".join(user_opts)))
            opt_file.write("\n")

    def build(self):
        """Determine which targets to build, then build them"""
        cross_targets_to_build, host_targets_to_build = self.get_build_targets()

        if not cross_targets_to_build and not host_targets_to_build:
            logging.error("no targets to build")
            sys.exit(1)

        logging.debug(
            "Building the following device targets:\n%s",
            "\n".join([t.bazel_label for t in cross_targets_to_build])
        )
        logging.debug(
            "Building the following host targets:\n%s",
            "\n".join([t.bazel_label for t in host_targets_to_build])
        )

        self.clean_legacy_generated_files()

        if self.skip_list:
            self.user_opts.extend(["--//msm-kernel:skip_{}=true".format(s) for s in self.skip_list])

        self.user_opts.extend([
            "--user_kmi_symbol_lists=//msm-kernel:android/abi_gki_aarch64_qcom",
            "--ignore_missing_projects",
        ])

        if self.dry_run:
            self.user_opts.append("--nobuild")

        device_user_opts = self.user_opts + ["--config=android_arm64"]

        logging.info("Building device targets...")
        self.build_targets(
            cross_targets_to_build,
            user_opts=device_user_opts,
        )

        if not self.dry_run:
            self.run_targets(
                cross_targets_to_build,
                user_opts=device_user_opts,
            )

        logging.info("Building host targets...")
        self.build_targets(
            host_targets_to_build, user_opts=self.user_opts
        )

        if not self.dry_run:
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
    parser.add_argument(
        "-d",
        "--dry-run",
        action="store_true",
        help="Perform a dry-run of the build which will perform loading/analysis of build files",
    )

    args, user_opts = parser.parse_known_args(sys.argv[1:])

    logging.basicConfig(
        level=getattr(logging, args.log.upper()),
        format="[{}] %(levelname)s: %(message)s".format(os.path.basename(sys.argv[0])),
    )

    args.skip.extend(DEFAULT_SKIP_LIST)

    builder = BazelBuilder(args.target, args.skip, args.out_dir, args.dry_run, user_opts)
    try:
        if args.menuconfig:
            builder.run_menuconfig()
        else:
            builder.build()
    except KeyboardInterrupt:
        logging.info("Received keyboard interrupt... exiting")
        del builder
        sys.exit(1)

    if args.dry_run:
        logging.info("Dry-run completed successfully!")
    else:
        logging.info("Build completed successfully!")

if __name__ == "__main__":
    main()
