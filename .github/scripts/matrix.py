#!/usr/bin/env python3

import os
import dataclasses
import json

from enum import Enum
from typing import Any, Dict, List, Final, Set, Union

MANAGED_OWNER: Final[str] = "kernel-patches"
MANAGED_REPOS: Final[Set[str]] = {
    f"{MANAGED_OWNER}/bpf",
    f"{MANAGED_OWNER}/vmtest",
}

DEFAULT_RUNNER: Final[str] = "ubuntu-24.04"
DEFAULT_LLVM_VERSION: Final[int] = 17
DEFAULT_SELF_HOSTED_RUNNER_TAGS: Final[List[str]] = ["self-hosted", "docker-noble-main"]


class Arch(str, Enum):
    """
    CPU architecture supported by CI.
    """

    AARCH64 = "aarch64"
    S390X = "s390x"
    X86_64 = "x86_64"


class Compiler(str, Enum):
    GCC = "gcc"
    LLVM = "llvm"


@dataclasses.dataclass
class Toolchain:
    compiler: Compiler
    # This is relevant ONLY for LLVM and should not be required for GCC
    version: int

    @property
    def short_name(self) -> str:
        return str(self.compiler.value)

    @property
    def full_name(self) -> str:
        if self.compiler == Compiler.GCC:
            return self.short_name

        return f"{self.short_name}-{self.version}"

    def to_dict(self) -> Dict[str, Union[str, int]]:
        return {
            "name": self.short_name,
            "fullname": self.full_name,
            "version": self.version,
        }


@dataclasses.dataclass
class BuildConfig:
    arch: Arch
    toolchain: Toolchain
    kernel: str = "LATEST"
    run_veristat: bool = False
    parallel_tests: bool = False
    build_release: bool = False

    @property
    def runs_on(self) -> List[str]:
        if is_managed_repo():
            return DEFAULT_SELF_HOSTED_RUNNER_TAGS + [self.arch.value]
        return [DEFAULT_RUNNER]

    @property
    def build_runs_on(self) -> List[str]:
        if is_managed_repo():
            # Build s390x on x86_64
            return DEFAULT_SELF_HOSTED_RUNNER_TAGS + [
                self.arch.value == "s390x" and Arch.X86_64.value or self.arch.value,
            ]
        return [DEFAULT_RUNNER]

    @property
    def tests(self) -> Dict[str, Any]:
        tests_list = [
            "test_progs",
            "test_progs_parallel",
            "test_progs_no_alu32",
            "test_progs_no_alu32_parallel",
            "test_verifier",
        ]

        if self.arch.value != "s390x":
            tests_list.append("test_maps")

        if self.toolchain.version >= 18:
            tests_list.append("test_progs_cpuv4")

        # if self.arch in [Arch.X86_64, Arch.AARCH64]:
        #     tests_list.append("sched_ext")

        # Don't run GCC BPF runner, because too many tests are failing
        # See: https://lore.kernel.org/bpf/87bjw6qpje.fsf@oracle.com/
        # if self.arch == Arch.X86_64:
        #    tests_list.append("test_progs-bpf_gcc")

        if not self.parallel_tests:
            tests_list = [test for test in tests_list if not test.endswith("parallel")]

        return {"include": [generate_test_config(test) for test in tests_list]}

    def to_dict(self) -> Dict[str, Any]:
        return {
            "arch": self.arch.value,
            "toolchain": self.toolchain.to_dict(),
            "kernel": self.kernel,
            "run_veristat": self.run_veristat,
            "parallel_tests": self.parallel_tests,
            "build_release": self.build_release,
            "runs_on": self.runs_on,
            "tests": self.tests,
            "build_runs_on": self.build_runs_on,
        }


def is_managed_repo() -> bool:
    return (
        os.environ["GITHUB_REPOSITORY_OWNER"] == MANAGED_OWNER
        and os.environ["GITHUB_REPOSITORY"] in MANAGED_REPOS
    )


def set_output(name, value):
    """Write an output variable to the GitHub output file."""
    with open(os.getenv("GITHUB_OUTPUT"), "a", encoding="utf-8") as file:
        file.write(f"{name}={value}\n")


def generate_test_config(test: str) -> Dict[str, Union[str, int]]:
    """Create the configuration for the provided test."""
    is_parallel = test.endswith("_parallel")
    config = {
        "test": test,
        "continue_on_error": is_parallel,
        # While in experimental mode, parallel jobs may get stuck
        # anywhere, including in user space where the kernel won't detect
        # a problem and panic. We add a second layer of (smaller) timeouts
        # here such that if we get stuck in a parallel run, we hit this
        # timeout and fail without affecting the overall job success (as
        # would be the case if we hit the job-wide timeout). For
        # non-experimental jobs, 360 is the default which will be
        # superseded by the overall workflow timeout (but we need to
        # specify something).
        "timeout_minutes": 30 if is_parallel else 360,
    }
    return config


if __name__ == "__main__":
    matrix = [
        BuildConfig(
            arch=Arch.X86_64,
            toolchain=Toolchain(compiler=Compiler.GCC, version=DEFAULT_LLVM_VERSION),
            run_veristat=True,
            parallel_tests=True,
        ),
        BuildConfig(
            arch=Arch.X86_64,
            toolchain=Toolchain(compiler=Compiler.LLVM, version=DEFAULT_LLVM_VERSION),
            build_release=True,
        ),
        BuildConfig(
            arch=Arch.X86_64,
            toolchain=Toolchain(compiler=Compiler.LLVM, version=18),
            build_release=True,
        ),
        BuildConfig(
            arch=Arch.AARCH64,
            toolchain=Toolchain(compiler=Compiler.GCC, version=DEFAULT_LLVM_VERSION),
        ),
        # BuildConfig(
        #     arch=Arch.AARCH64,
        #     toolchain=Toolchain(
        #         compiler=Compiler.LLVM,
        #         version=DEFAULT_LLVM_VERSION
        #     ),
        # ),
        BuildConfig(
            arch=Arch.S390X,
            toolchain=Toolchain(compiler=Compiler.GCC, version=DEFAULT_LLVM_VERSION),
        ),
    ]

    # Outside of those repositories we only run on x86_64
    if not is_managed_repo():
        matrix = [config for config in matrix if config.arch == Arch.X86_64]

    json_matrix = json.dumps({"include": [config.to_dict() for config in matrix]})
    print(json_matrix)
    set_output("build_matrix", json_matrix)
