import os
import importlib.util
from glob import glob

class KernelNotFoundError(Exception):
    def __init__(self):
        kernel_version = os.uname().release
        super().__init__(f"WARNING: python perf module not found for kernel {kernel_version}\n\n"
                         f"You may need to install the following packages for this specific kernel:\n"
                         f"  linux-tools-{kernel_version}-generic\n"
                         f"You may also want to install of the following package to keep up to date:\n"
                         f"  linux-tools-generic")

# Extract ABI number from kernel version
def _get_abi_version():
    _kernel_version = os.uname().release
    _parts = _kernel_version.split("-")
    return "-".join(_parts[:-1])

# Load the actual python-perf module for the running kernel
_abi_version = _get_abi_version()
_perf_dir = f"/usr/lib/python3/dist-packages/linux-tools-{_abi_version}"
if not os.path.exists(_perf_dir):
    raise KernelNotFoundError()
_perf_lib = glob(os.path.join(_perf_dir, "*.so"))[-1]

_spec = importlib.util.spec_from_file_location("perf", _perf_lib)
_perf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_perf)

# Expose the 'perf' module.
__all__ = ['perf']
