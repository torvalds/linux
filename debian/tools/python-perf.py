import os
import importlib.util
from glob import glob


class KernelNotFoundError(Exception):
    def __init__(self):
        kernel_version = os.uname().release
        flavor = kernel_version.split("-", 2)[2]
        super().__init__(
            f"\nWARNING: python perf module not found for kernel {kernel_version}\n\n"
            f"  You may need to install the following package for this specific kernel:\n"
            f"    linux-tools-{kernel_version}\n\n"
            f"  You may also want to install the following package to keep up to date:\n"
            f"    linux-tools-{flavor}"
        )


# Load the actual python-perf module for the running kernel
_kernel_version = os.uname().release
_perf_dir = f"/usr/lib/linux-tools/{_kernel_version}/lib"
if not os.path.exists(_perf_dir):
    raise KernelNotFoundError()
_perf_lib = glob(os.path.join(_perf_dir, "perf.*.so"))[-1]

_spec = importlib.util.spec_from_file_location("perf", _perf_lib)
_perf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_perf)

# Expose the 'perf' module.
__all__ = ["perf"]
