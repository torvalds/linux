from asyncio import SendfileNotAvailableError
import smtplib
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText

import libs

class EmailTool:

    def __init__(self, server=None, port=None, sender=None, receivers=[],
                 startls=True, token=None, config=None):
        self._server = server
        self._port = port
        self._sender = sender
        self._receivers = receivers
        self._starttls = startls
        self._token = token
        self._message = MIMEMultipart()

        if config:
            if 'server' in config:
                self._server = config['server']
            if 'port' in config:
                self._port = config['port']
            if 'user' in config:
                self._sender = config['user']
            if 'startls' in config:
                self._startls = config['startls']

    def send(self):
        try:
            session = smtplib.SMTP(self._server, self._port)
            session.ehlo()
            if self._starttls:
                session.starttls()
            session.ehlo()
            session.login(self._sender, self._token)
            session.sendmail(self._sender, self._receivers, self._message.as_string())
        except Exception as e:
            libs.log_error("Failed to Send email")
            libs.log_error(e)
        finally:
            session.quit()

        libs.log_info("Email sent successfully")

    def set_receivers(self, receivers):
        self._receivers = receivers
        libs.log_info("Receivers are updated")

    def set_token(self, token):
        self._token = token
        libs.log_info("Email Token is updated")

    def _update_header(self, headers):
        for key, value in headers.items():
            self._message.add_header(key, value)

    def compose(self, title, body, headers):
        self._message['From'] = self._sender
        self._message['To'] = ", ".join(self._receivers)
        self._message['Subject'] = title
        self._message.attach(MIMEText(body, 'plain'))
        self._update_header(headers)

        libs.log_debug(f"EMAIL Message: \n{self._message}")
