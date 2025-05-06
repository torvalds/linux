import logging
import os
import subprocess
import time
import re
from typing import List, Dict, Tuple

# Global logging object
logger = None

def init_logger(name, verbose=False):
    global logger

    logger = logging.getLogger(name)
    logger.setLevel(logging.INFO)
    if verbose:
        logger.setLevel(logging.DEBUG)

    ch = logging.StreamHandler()
    formatter = logging.Formatter('%(asctime)s:%(levelname)-8s:%(message)s')
    ch.setFormatter(formatter)

    logger.addHandler(ch)

    logger.info("Logger initialized: level=%s",
                logging.getLevelName(logger.getEffectiveLevel()))

def log_info(msg):
    if logger is not None:
        logger.info(msg)

def log_error(msg):
    if logger is not None:
        logger.error(msg)

def log_debug(msg):
    if logger is not None:
        logger.debug(msg)

def pr_get_sid(pr_title):
    """
    Parse PR title prefix and get PatchWork Series ID
    PR Title Prefix = "[PW_S_ID:<series_id>] XXXXX"
    """

    try:
        sid = re.search(r'^\[PW_SID:([0-9]+)\]', pr_title).group(1)
    except AttributeError:
        log_error(f"Unable to find the series_id from title {pr_title}")
        sid = None

    return sid

def cmd_run(cmd: List[str], shell: bool = False, add_env: Dict[str, str] = None,
            cwd: str = None, pass_fds=()) -> Tuple[str, str, str]:
    log_info(f"------------- CMD_RUN -------------")
    log_info(f"CMD: {cmd}")

    stdout = ""

    # Update ENV
    env = os.environ.copy()
    if add_env:
        env.update(add_env)

    start_time = time.time()

    proc = subprocess.Popen(cmd, shell=shell, env=env, cwd=cwd,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            bufsize=1, universal_newlines=True,
                            pass_fds=pass_fds)
    log_debug(f"PROC args: {proc.args}")

    # Print the stdout in realtime
    for line in proc.stdout:
        log_debug("> " + line.rstrip('\n'))
        stdout += line

    # STDOUT returned by proc.communicate() is empty because it was all consumed
    # by the above read.
    _stdout, stderr = proc.communicate()
    proc.stdout.close()
    proc.stderr.close()

    stderr = "\n" + stderr
    if stderr[-1] == "\n":
        stderr = stderr[:-1]

    log_info(f'RET: {proc.returncode}')
    # No need to print STDOUT here again. It is already printed above
    # log_debug(f'STDOUT:{stdout}')
    # Print STDOUT only if ret != 0
    if proc.returncode:
        log_debug(f'STDERR:{stderr}')

    if proc.returncode != 0:
        if stderr and stderr[:-1] == "\n":
            stderr = stderr[:-1]

    elapsed = time.time() - start_time

    log_info(f"------------- CMD_RUN END ({elapsed:.2f} s) -------------")
    return proc.returncode, stdout, stderr


