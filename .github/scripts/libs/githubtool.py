from github import Github, InputFileContent
import re

class GithubTool:

    def __init__(self, repo, token=None, gist_token=None):
        self._repo = Github(token).get_repo(repo)
        self._user = Github(gist_token).get_user()
        self._pr = None
        self._prs = None

    def get_pr_commits(self, pr_id):
        pr = self.get_pr(pr_id, True)

        return pr.get_commits()

    def get_pr(self, pr_id, force=False):
        if force or self._pr == None:
            self._pr = self._repo.get_pull(pr_id)

        return self._pr

    def get_prs(self, force=False):
        if force or not self._prs:
            self._prs = self._repo.get_pulls()

        return self._prs

    def create_pr(self, title, body, base, head):

        return self._repo.create_pull(base, head, title=title, body=body,
                                      maintainer_can_modify=True)

    def close_pr(self, pr_id):
        pr = self.get_pr(pr_id, force=True)
        pr.edit(state="closed")

        git_ref = self._repo.get_git_ref(f"heads/{pr.head.ref}")
        git_ref.delete()

    def pr_exist_title(self, str):
        if not self._prs:
            self._prs = self.get_prs(force=True)

        for pr in self._prs:
            if re.search(str, pr.title, re.IGNORECASE):
                return True

        return False

    def pr_post_comment(self, pr, comment):

        try:
            pr.create_issue_comment(comment)
        except:
            return False

        return True

    def pr_get_issue_comments(self, pr):
        try:
            comments = pr.get_issue_comments()
        except:
            return None

        return comments

    def pr_close(self, pr):
        pr.edit(state="closed")

    def create_gist(self, title, test, body):
        gist = self._user.create_gist(
            public=True,
            description=title,
            files={test: InputFileContent(body)})

        return gist.html_url
