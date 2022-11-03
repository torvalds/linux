#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

import argparse
import logging
import os
import re
import sys
import subprocess

CPU = "aarch64"
HOST_CROSSTOOL = "@bazel_tools//tools/cpp:toolchain"
HOST_TARGETS = ["dtc"]
DEFAULT_SKIP_LIST = ["abi", "test_mapping"]


class BazelBuilder:
    """Helper class for building with Bazel"""

    def __init__(self, target_list, skip_list, user_opts):
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
        self.target_list = target_list
        self.skip_list = skip_list
        self.user_opts = user_opts
        self.process_list = []

    def __del__(self):
        for proc in self.process_list:
            proc.kill()
            proc.wait()

    @staticmethod
    def get_cross_cli_opts(toolchain):
        """Form cross toolchain Bazel options"""
        return [
            "--cpu={}".format(CPU),
            "--crosstool_top={}".format(toolchain),
            "--host_crosstool_top={}".format(HOST_CROSSTOOL),
        ]

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
                target_list = query_cmd.stdout.read().splitlines()
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

        return (cross_target_list, host_target_list)

    def get_cross_toolchain(self):
        """Query for a custom toolchain if one is defined"""
        logging.info("Querying toolchains...")
        query = 'filter("crosstool_suite", {}/...)'.format(self.kernel_dir)
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
            toolchain = query_cmd.stdout.read().strip()
        except Exception as e:
            logging.error(e)
            sys.exit(1)

        self.process_list.remove(query_cmd)

        if not toolchain:
            logging.debug("no userspace cross toolchain found")
            return None

        logging.debug("using userspace cross toolchain %s", toolchain)
        return toolchain

    def bazel(
        self, bazel_subcommand, targets, extra_options=None, us_cross_toolchain=None
    ):
        """Execute a bazel command"""
        cmdline = [self.bazel_bin, bazel_subcommand]
        if extra_options:
            cmdline.extend(extra_options)
        if us_cross_toolchain:
            cmdline.extend(self.get_cross_cli_opts(us_cross_toolchain))
        cmdline.extend(targets)
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

    def build_targets(self, targets, user_opts=None, us_cross_toolchain=None):
        """Run "bazel build" on all targets in parallel"""
        if not targets:
            logging.warning("no targets to build")
        self.bazel("build", targets, user_opts, us_cross_toolchain)

    def run_targets(self, targets, user_opts=None, us_cross_toolchain=None):
        """Run "bazel run" on all targets in serial (since bazel run cannot have multiple targets)"""
        for target in targets:
            self.bazel("run", [target], user_opts, us_cross_toolchain)

    def build(self):
        """Determine which targets to build, then build them"""
        cross_targets_to_build, host_targets_to_build = self.get_build_targets()

        logging.debug(
            "Building the following %s targets:\n%s",
            CPU,
            "\n".join(cross_targets_to_build),
        )
        logging.debug(
            "Building the following host targets:\n%s", "\n".join(host_targets_to_build)
        )

        us_cross_toolchain = self.get_cross_toolchain()

        if not cross_targets_to_build and not host_targets_to_build:
            logging.error("no targets to build")
            sys.exit(1)

        logging.info("Building %s targets...", CPU)
        self.build_targets(cross_targets_to_build, self.user_opts, us_cross_toolchain)
        self.run_targets(cross_targets_to_build, self.user_opts, us_cross_toolchain)

        logging.info("Building host targets...")
        self.build_targets(host_targets_to_build, self.user_opts)
        self.run_targets(host_targets_to_build, self.user_opts)


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
        "--log",
        metavar="LEVEL",
        default="info",
        choices=["debug", "info", "warning", "error"],
        help="Log level (debug, info, warning, error)",
    )

    args, user_opts = parser.parse_known_args(sys.argv[1:])

    logging.basicConfig(
        level=getattr(logging, args.log.upper()),
        format="[{}] %(levelname)s: %(message)s".format(os.path.basename(sys.argv[0])),
    )

    args.skip.extend(DEFAULT_SKIP_LIST)

    builder = BazelBuilder(args.target, args.skip, user_opts)
    try:
        builder.build()
    except KeyboardInterrupt:
        logging.info("Received keyboard interrupt... exiting")
        del builder
        sys.exit(1)

    logging.info("Build completed successfully!")


if __name__ == "__main__":
    main()
