import os
import json

from libs import EmailTool, GithubTool, Patchwork, RepoTool
from libs import log_info, log_debug, log_error


class ContextError(Exception):
    pass


class Context():
    """Collection of data for bzcafe. It is useful for CI"""

    def __init__(self, config_file=None, github_repo=None, src_dir=None,
                 patch_root=None, **kwargs):

        # Init config
        log_info(f"Initialize config file: {config_file}")
        self.config = None
        if config_file:
            with open(os.path.abspath(config_file), 'r') as f:
                self.config = json.load(f)

        # Init patchwork
        log_info("Initialize patchwork")
        try:
            self.pw = Patchwork(self.config['patchwork']['url'],
                                self.config['patchwork']['project_name'])
        except:
            log_error("Failed to initialize Patchwork class")
            raise ContextError

        # If token and username is available, set it here
        if 'PATCHWORK_TOKEN' in os.environ and os.environ['PATCHWORK_TOKEN'] != "":
            log_debug("Found Patchwork Token in environment variable")
            self.pw.set_token(os.environ['PATCHWORK_TOKEN'])

        if 'PATCHWORK_USER' in os.environ and os.environ['PATCHWORK_USER'] != "":
            log_debug("Found Patchwork User in environment variable")
            self.pw.set_user(int(os.environ['PATCHWORK_USER']))

        # Init github
        log_info(f"Initialize Github: {github_repo}")
        if 'GITHUB_TOKEN' not in os.environ:
            log_error("Set GITHUB_TOKEN environment variable")
            raise ContextError

        if 'GIST_TOKEN' not in os.environ:
            log_error("Set GIST_TOKEN environment variable")
            raise ContextError

        try:
            self.gh = GithubTool(github_repo, os.environ['GITHUB_TOKEN'],
                                 os.environ['GIST_TOKEN'])
        except:
            log_error("Failed to initialize GithubTool class")
            raise ContextError

        # Init email
        log_info("Initailze EmailTool")
        token = None
        if 'EMAIL_TOKEN' in os.environ:
            token = os.environ['EMAIL_TOKEN']
            log_info("Email Token is read from environment variable")

        self.email = EmailTool(token=token, config=self.config['email'])

        # Init src_dir
        log_info(f"Initialize Source directory: {src_dir}")
        try:
            self.src_repo = RepoTool(os.path.basename(src_dir), src_dir)
        except:
            log_error("Failed to initialize RepoTool class")
            raise ContextError
        self.src_dir = self.src_repo.path()
        self.patch_root = patch_root

        # Custome confguration
        for kw in kwargs:
            log_info(f"Storing {kw}:{kwargs[kw]}")
            self.config[kw] = kwargs[kw]

        # These are the frequently used variables by CI
        self.series = None
        self.patches = None
        self.shas = None

        log_info("Context Initialization Completed")

    def update_series(self, series, shas):
        self.series = series
        self.patches = series['patches']
        self.shas = shas

