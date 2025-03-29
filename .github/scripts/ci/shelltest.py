from gettext import install
import os
import sys

sys.path.insert(0, '../libs')
from libs import RepoTool, cmd_run

from ci import Base, Verdict, EndTest, submit_pw_check

class ShellTest(Base):
    """Run shell test class
    This class runs a shell based test
    """

    def __init__(self, ci_data, patch, name, desc, sh):

        # Common
        self.name = name
        self.desc = desc
        self.ci_data = ci_data

        self.sh = sh
        self.patch = patch

        super().__init__()

        self.log_dbg("Initialization completed")

    def run(self, worktree=None):

        self.log_dbg("Run")
        self.start_timer()

        current_script_path = os.path.dirname(os.path.abspath(__file__))

        cwd = worktree if worktree else self.ci_data.src_dir
        cmd = ["bash", f"{current_script_path}/../pw_tests/{self.sh}"]
        (ret, stdout, stderr) = cmd_run(cmd, cwd=cwd)

        if ret == 0:
            submit_pw_check(self.ci_data.pw, self.patch,
                            self.name, Verdict.PASS,
                            self.name,
                            None, self.ci_data.config['dry_run'])
            self.success()
        elif ret == 250:
            url = self.ci_data.gh.create_gist(f"pw{self.ci_data.series['id']}-p{self.patch['id']}",
                                              f"{self.name}-WARNING",
                                              stdout + '\n' + stderr)
            submit_pw_check(self.ci_data.pw, self.patch,
                            self.name, Verdict.WARNING,
                            self.name,
                            url, self.ci_data.config['dry_run'])
            self.warning(stdout + '\n' + stderr)
        else:
            url = self.ci_data.gh.create_gist(f"pw{self.ci_data.series['id']}-p{self.patch['id']}",
                                              f"{self.name}-FAIL",
                                              stdout + '\n' + stderr)
            submit_pw_check(self.ci_data.pw, self.patch,
                            self.name, Verdict.FAIL,
                            self.name,
                            url, self.ci_data.config['dry_run'])
            self.error(stdout + '\n' + stderr)

    def post_run(self):

        self.log_dbg("Post Run...")
