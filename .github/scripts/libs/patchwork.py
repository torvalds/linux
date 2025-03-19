import datetime
import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

import libs

class PostException(Exception):
    pass


class Patchwork():

    def __init__(self, server, project_name, user=None, token=None, api=None):
        self._session = requests.Session()
        retry = Retry(connect=10, backoff_factor=1)
        adapter = HTTPAdapter(max_retries=retry)
        self._session.mount('http://', adapter)
        self._session.mount('https://', adapter)
        self._server = server
        self._token = token
        self._user = user
        self._project_name = project_name
        self._api = "/api" if api == None else f"/api/{api}"
        self._project_id= self._get_project_id(project_name)

        libs.log_info(f"Connected to Patchwork Server: {self._server}: {self._project_id}")

    def set_token(self, token):
        self._token = token

    def set_user(self, user):
        self._user = user

    def _request(self, url):
        libs.log_debug(f"PW GET URL: {url}")
        resp = self._session.get(url)
        if resp.status_code != 200:
            raise requests.HTTPError(f"GET {resp.status_code}")

        return resp

    def _get(self, req):
        return self._request(f'{self._server}{self._api}/{req}')

    def _get_project(self, name):
        projects = self.get_all('projects')
        for project in projects:
            if project['name'] == name:
                return project

        libs.log_error(f"No matched project found: {name}")
        return None

    def _get_project_id(self, name):
        project = self._get_project(name)
        if project:
            return project['id']

        raise ValueError

    def _post(self, req, headers, data):
        url = f'{self._server}{self._api}/{req}'
        libs.log_debug(f"PW POST URL: {url}")
        return self._session.post(url, headers=headers, data=data)

    def get(self, type, identifier):
        return self._get(f'{type}/{identifier}/').json()

    def get_all(self, type, filters=None):
        if filters is None:
            filters={}
        params = ''
        for key, val in filters.items():
            if val is not None:
                params += f'{key}={val}&'

        items = []

        response = self._get(f'{type}/?{params}')
        while response:
            for entry in response.json():
                items.append(entry)

            if 'Link' not in response.headers:
                break

            links = response.headers['Link'].split(',')
            response = None
            for link in links:
                info = link.split(';')
                if info[1].strip() == 'rel="next"':
                    response = self._request(info[0][1:-1])

        return items

    def post_check(self, patch, context, state, desc, url=None):
        headers = {}
        if self._token:
            headers['Authorization'] = f'Token {self._token}'

        data = {
            'user': self._user,
            'state': state,
            'target_url': url if url else "",
            'context': context,
            'description': desc
        }

        resp = self._post(f"patches/{patch['id']}/checks/", data=data,
                          headers=headers)
        if resp.status_code != 201 and resp.status_code != 200:
            libs.log_error(f"PW POST failed: {resp.status_code}")
            raise PostException(f"POST {resp.status_code}")

    def get_series_mbox(self, id):
        url = f'{self._server}/series/{id}/mbox/'
        return self._request(url).content.decode()

    def get_patch_mbox(self, id):
        patch = self.get_patch(id)
        return self._request(patch['mbox']).content.decode()

    def get_series(self, series_id):
        return self.get('series', series_id)

    def get_patch(self, patch_id):
        return self.get('patches', patch_id)

    def get_patches_by_state(self, state, archived=False, days_lookback=0):
        filter = {}

        filter['project'] = self._project_id
        filter['state'] = state
        filter['archived'] = 'true' if archived else 'false'
        if days_lookback > 0:
            today = datetime.datetime.utcnow().date()
            lookback = today - datetime.timedelta(days=days_lookback)
            filter['since'] = lookback.strftime("%Y-%m-%dT%H:%M:%S")


        return self.get_all('patches', filter)

    def get_series_by_state(self, state, archived=False, days_lookback=0):
        series_ids = []
        series_list = []

        patches = self.get_patches_by_state(state, archived, days_lookback)
        if len(patches) == 0:
            return series

        for patch in patches:
            # Skip if patch has no series
            if 'series' not in patch:
                continue

            for series in patch['series']:
                # Check if series id already exist
                if series['id'] not in series_ids:
                    series_ids.append(series['id'])
                    series_list.append(self.get_series(series['id']))

        return series_list

    def save_patch_mbox(self, patch_id, filename):
        patch_mbox = self.get_patch_mbox(patch_id)

        with open(filename, 'w+') as f:
            f.write(patch_mbox)

        return filename

    def save_patch_msg(self, patch_id, filename):
        patch = self.get_patch(patch_id)

        with open(filename, 'w+') as f:
            f.write(patch['name'])
            f.write('\n\n')
            f.write(patch['content'])

        return filename
