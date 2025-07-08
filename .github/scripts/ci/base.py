from abc import ABC, abstractmethod
from enum import Enum
import time
import sys

from libs import utils

sys.path.insert(0, '../libs')
from libs import log_debug

class Verdict(Enum):
    PENDING = 0
    PASS = 1
    FAIL = 2
    ERROR = 3
    SKIP = 4
    WARNING = 5


class EndTest(Exception):
    """
    End of Test
    """

class Base(ABC):
    """
    Base class for CI Tests.
    """
    def __init__(self):
        self.start_time = 0
        self.end_time = 0
        self.verdict = Verdict.PENDING
        self.output = ""

    def success(self):
        self.end_timer()
        self.verdict = Verdict.PASS

    def error(self, msg):
        self.verdict = Verdict.ERROR
        self.output = msg
        self.end_timer()
        raise EndTest

    def warning(self, msg):
        self.verdict = Verdict.WARNING
        self.output = msg
        self.end_timer()

    def skip(self, msg):
        self.verdict = Verdict.SKIP
        self.output = msg
        self.end_timer()
        raise EndTest

    def add_failure(self, msg):
        self.verdict = Verdict.FAIL
        if not self.output:
            self.output = msg
        else:
            self.output += "\n" + msg

    def add_failure_end_test(self, msg):
        self.add_failure(msg)
        self.end_timer()
        raise EndTest

    def start_timer(self):
        self.start_time = time.time()

    def end_timer(self):
        self.end_time = time.time()

    def elapsed(self):
        if self.start_time == 0:
            return 0
        if self.end_time == 0:
            self.end_timer()
        return self.end_time - self.start_time

    def log_err(self, msg):
        utils.log_error(f"CI: {self.name}: {msg}")

    def log_info(self, msg):
        utils.log_info(f"CI: {self.name}: {msg}")

    def log_dbg(self, msg):
        utils.log_debug(f"CI: {self.name}: {msg}")

    @abstractmethod
    def run(self, worktree=None):
        """
        The child class should implement run() method
        If the test fail, it should raise the EndTest exception
        """
        pass

    @abstractmethod
    def post_run(self):
        """
        The child class should implement post_run() method
        """
        pass


def submit_pw_check(pw, patch, name, verdict, desc, url=None, dry_run=False):

    utils.log_debug(f"Submitting the result to PW: dry_run={dry_run}")

    if not dry_run:
        state = 0

        if verdict == Verdict.PASS:
            state = 1
        if verdict == Verdict.WARNING:
            state = 2
        if verdict == Verdict.FAIL:
            state = 3

        pw.post_check(patch, name, state, desc, url)
