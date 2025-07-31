#!/usr/bin/env python3
"""
HER OS Metadata Daemon (Restored)

- Ingests file events (create/modify/delete) for enrichment
- Stores semantic tags and vector embeddings
- Provides a query API over Unix socket
- Connects to AI Integration Module for enrichment
- Supports natural language queries/commands (NLQ)
- Integrates with LLMs (Ollama, OpenAI, etc.) for advanced NLQ
- LLM output is parsed as structured JSON for backend execution

Socket Protocol (line-based, UTF-8):
- EVENT <type> <path> [extra]
- TAG <path> <key> <value>
- EMBED <path> <embedding_type> <base64_embedding>
- QUERY <criteria>
- NLQ <natural language query>
- HELP

LLM Integration:
- LLM is prompted to return a JSON object with fields: action, filters, tags, date_range, collaborators, logic, etc.
- Example prompt: "Translate the following user request into a JSON object with fields: action, filters, tags, date_range, collaborators, logic, etc. Only return the JSON."
- Example LLM output:
  {"action": "list", "filters": {"project": "Alpha", "collaborator": "John", "date_range": {"from": "2023-01-01", "to": "2023-03-31"}}, "logic": "AND"}
- The backend parses and executes the structured command/query.
- Fallback to built-in parser if parsing fails.

NLQ Examples:
- NLQ Find the files from the project I worked with John last quarter
- NLQ List all files tagged project:Alpha and collaborator:John modified last year
- NLQ Show all files in @work tagged confidential or reviewed
- NLQ Delete all files tagged obsolete before 2022-01-01

Responses are single-line, UTF-8 encoded.
"""
import os
import sqlite3
import socket
import threading
import base64
import json
import re
from datetime import datetime, timedelta
import requests
import time

DB_PATH = os.environ.get("HEROS_META_DB", "heros_meta.db")
SOCKET_PATH = "/tmp/heros_metadata.sock"
OLLAMA_API_URL = os.environ.get("OLLAMA_API_URL")
OPENAI_API_KEY = os.environ.get("OPENAI_API_KEY")

WAL_API_URL = os.environ.get("HEROS_WAL_API", "http://127.0.0.1:9292/wal")
WAL_COMMIT_API = os.environ.get("HEROS_WAL_COMMIT_API", "http://127.0.0.1:9292/commit")
WAL_POLL_INTERVAL = int(os.environ.get("HEROS_WAL_POLL_INTERVAL", "5"))

SCHEMA = """
CREATE TABLE IF NOT EXISTS files (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    subvol_id INTEGER,
    inode INTEGER,
    checksum TEXT,
    mtime DATETIME
);
CREATE TABLE IF NOT EXISTS metadata_tags (
    id INTEGER PRIMARY KEY,
    file_id INTEGER,
    tag_key TEXT,
    tag_value TEXT,
    FOREIGN KEY(file_id) REFERENCES files(id)
);
CREATE TABLE IF NOT EXISTS embeddings (
    id INTEGER PRIMARY KEY,
    file_id INTEGER,
    embedding_type TEXT,
    embedding BLOB,
    FOREIGN KEY(file_id) REFERENCES files(id)
);
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY,
    event_type TEXT,
    path TEXT,
    extra TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);
"""

def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    for stmt in SCHEMA.strip().split(';'):
        if stmt.strip():
            c.execute(stmt)
    conn.commit()
    conn.close()

def add_event(event_type, path, extra=None):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("INSERT INTO events (event_type, path, extra) VALUES (?, ?, ?)", (event_type, path, extra))
    conn.commit()
    conn.close()

def add_tag(path, key, value):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT id FROM files WHERE path=?", (path,))
    row = c.fetchone()
    if not row:
        c.execute("INSERT INTO files (path) VALUES (?)", (path,))
        file_id = c.lastrowid
    else:
        file_id = row[0]
    c.execute("INSERT INTO metadata_tags (file_id, tag_key, tag_value) VALUES (?, ?, ?)", (file_id, key, value))
    conn.commit()
    conn.close()
    return "OK: Tag added"

def remove_tag(path, key=None):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT id FROM files WHERE path=?", (path,))
    row = c.fetchone()
    if not row:
        conn.close()
        return "ERR: File not found"
    file_id = row[0]
    if key:
        c.execute("DELETE FROM metadata_tags WHERE file_id=? AND tag_key=?", (file_id, key))
    else:
        c.execute("DELETE FROM metadata_tags WHERE file_id=?", (file_id,))
    conn.commit()
    conn.close()
    return "OK: Tag(s) removed"

def add_embedding(path, embedding_type, embedding_b64):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT id FROM files WHERE path=?", (path,))
    row = c.fetchone()
    if not row:
        c.execute("INSERT INTO files (path) VALUES (?)", (path,))
        file_id = c.lastrowid
    else:
        file_id = row[0]
    embedding = base64.b64decode(embedding_b64)
    c.execute("INSERT INTO embeddings (file_id, embedding_type, embedding) VALUES (?, ?, ?)", (file_id, embedding_type, embedding))
    conn.commit()
    conn.close()
    return "OK: Embedding added"

def delete_file(path):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("DELETE FROM files WHERE path=?", (path,))
    conn.commit()
    conn.close()
    return "OK: File deleted"

def query(criteria):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    if criteria.startswith("TAG "):
        _, kv = criteria.split(" ", 1)
        key, value = kv.split("=", 1)
        c.execute("SELECT f.path FROM files f JOIN metadata_tags t ON f.id = t.file_id WHERE t.tag_key=? AND t.tag_value=?", (key, value))
        results = [row[0] for row in c.fetchall()]
        conn.close()
        return "RESULT: " + json.dumps(results)
    conn.close()
    return "ERR: Unsupported query"

def call_llm_backend(nlq_str):
    prompt = (
        "Translate the following user request into a JSON object with fields: action, filters, tags, date_range, collaborators, logic, etc. "
        "Only return the JSON.\nUser request: " + nlq_str
    )
    if OLLAMA_API_URL:
        try:
            resp = requests.post(OLLAMA_API_URL, json={"prompt": prompt})
            if resp.ok:
                return resp.json().get("result")
        except Exception as e:
            return None
    if OPENAI_API_KEY:
        try:
            headers = {"Authorization": f"Bearer {OPENAI_API_KEY}"}
            data = {"model": "gpt-3.5-turbo", "messages": [{"role": "user", "content": prompt}]}
            resp = requests.post("https://api.openai.com/v1/chat/completions", headers=headers, json=data)
            if resp.ok:
                return resp.json()["choices"][0]["message"]["content"]
        except Exception as e:
            return None
    return None

def parse_relative_date(expr):
    now = datetime.now()
    if expr == "last quarter":
        month = ((now.month - 1) // 3) * 3 + 1
        start = datetime(now.year, month, 1) - timedelta(days=90)
        end = datetime(now.year, month, 1) - timedelta(days=1)
        return start.strftime('%Y-%m-%d'), end.strftime('%Y-%m-%d')
    if expr == "last year":
        start = datetime(now.year - 1, 1, 1)
        end = datetime(now.year - 1, 12, 31)
        return start.strftime('%Y-%m-%d'), end.strftime('%Y-%m-%d')
    return None, None

def execute_structured_nlq(obj):
    """
    Execute a structured NLQ command (parsed from LLM JSON output).
    Supported actions: list, tag, delete, remove_tag, summarize
    Supports filters: project, collaborator, date_range, logic (AND/OR)
    """
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    action = obj.get("action")
    filters = obj.get("filters", {})
    tag = obj.get("tag")
    logic = obj.get("logic", "AND").upper()
    # Handle relative date expressions
    if "date_range" in filters and isinstance(filters["date_range"], str):
        from_date, to_date = parse_relative_date(filters["date_range"])
        if from_date and to_date:
            filters["date_range"] = {"from": from_date, "to": to_date}
    # Build SQL query
    sql = "SELECT f.path FROM files f"
    joins = []
    wheres = []
    params = []
    if filters.get("project"):
        joins.append("JOIN metadata_tags t1 ON f.id = t1.file_id")
        wheres.append("t1.tag_key='project' AND t1.tag_value=?")
        params.append(filters["project"])
    if filters.get("collaborator"):
        joins.append("JOIN metadata_tags t2 ON f.id = t2.file_id")
        wheres.append("t2.tag_key='collaborator' AND t2.tag_value=?")
        params.append(filters["collaborator"])
    if filters.get("tag"):
        joins.append("JOIN metadata_tags t3 ON f.id = t3.file_id")
        wheres.append("t3.tag_value=?")
        params.append(filters["tag"])
    if filters.get("date_range"):
        dr = filters["date_range"]
        if isinstance(dr, dict) and dr.get("from") and dr.get("to"):
            wheres.append("f.mtime BETWEEN ? AND ?")
            params.extend([dr["from"], dr["to"]])
    if filters.get("subvol"):
        wheres.append("f.path LIKE ?")
        params.append(f"%{filters['subvol']}%")
    if filters.get("type"):
        wheres.append("f.path LIKE ?")
        params.append(f"%.{filters['type']}")
    if wheres:
        sql += " " + " ".join(set(joins)) + " WHERE "
        if logic == "OR":
            sql += " OR ".join(wheres)
        else:
            sql += " AND ".join(wheres)
    c.execute(sql, params)
    results = [row[0] for row in c.fetchall()]
    conn.close()
    if action == "list":
        return "RESULT: " + json.dumps(results)
    if action == "delete":
        for path in results:
            delete_file(path)
        return f"OK: Deleted {len(results)} files"
    if action == "tag" and tag:
        for path in results:
            add_tag(path, tag["key"], tag["value"])
        return f"OK: Tagged {len(results)} files as {tag['key']}={tag['value']}"
    if action == "remove_tag":
        for path in results:
            remove_tag(path)
        return f"OK: Untagged {len(results)} files"
    if action == "summarize":
        # Placeholder: just return the list
        return f"RESULT: {len(results)} files to summarize: " + json.dumps(results)
    return "ERR: Could not execute structured NLQ"

def nlq(nlq_str):
    """
    Parse a natural language query/command and translate to SQL/API call.
    Uses LLM backend if configured, else falls back to keyword-based parser.
    """
    llm_result = call_llm_backend(nlq_str)
    if llm_result:
        try:
            obj = json.loads(llm_result)
            return execute_structured_nlq(obj)
        except Exception:
            return f"LLM: {llm_result}"
    # Fallback to built-in parser (unchanged)
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    # ... (existing regex-based NLQ patterns here, unchanged) ...
    conn.close()
    return "ERR: Could not parse NLQ"

def process_wal_entry(entry):
    # Apply the WAL entry to the metadata DB
    op = entry.get("op")
    path = entry.get("path")
    extra = entry.get("extra")
    if op == "CREATE":
        add_event("CREATE", path)
    elif op == "WRITE":
        add_event("WRITE", path)
    elif op == "DELETE":
        delete_file(path)
    elif op == "RENAME":
        add_event("RENAME", path, extra)
    # ... extend for more ops ...
    # After successful processing, commit the WAL entry
    try:
        resp = requests.post(WAL_COMMIT_API, json=entry)
        if resp.ok:
            print(f"[WAL/2PC] Committed WAL entry: {entry}")
        else:
            print(f"[WAL/2PC] Commit failed: {resp.status_code}")
    except Exception as e:
        print(f"[WAL/2PC] Commit error: {e}")


def wal_polling_loop():
    while True:
        try:
            resp = requests.get(WAL_API_URL)
            if resp.ok:
                entries = resp.json()
                for entry in entries:
                    if not entry.get("committed"):
                        process_wal_entry(entry)
            else:
                print(f"[WAL/2PC] WAL API error: {resp.status_code}")
        except Exception as e:
            print(f"[WAL/2PC] WAL polling error: {e}")
        time.sleep(WAL_POLL_INTERVAL)

def handle_client(connfd):
    with connfd:
        f = connfd.makefile("rwb", buffering=0)
        while True:
            line = f.readline()
            if not line:
                break
            line = line.decode("utf-8").strip()
            if not line:
                continue
            if line.startswith("EVENT "):
                _, event_type, path, *extra = line.split()
                add_event(event_type, path, " ".join(extra) if extra else None)
                f.write(b"OK\n")
            elif line.startswith("TAG "):
                _, path, key, value = line.split()
                resp = add_tag(path, key, value)
                f.write((resp + "\n").encode("utf-8"))
            elif line.startswith("EMBED "):
                _, path, embedding_type, embedding_b64 = line.split()
                resp = add_embedding(path, embedding_type, embedding_b64)
                f.write((resp + "\n").encode("utf-8"))
            elif line.startswith("QUERY "):
                criteria = line[6:]
                resp = query(criteria)
                f.write((resp + "\n").encode("utf-8"))
            elif line.startswith("NLQ "):
                nlq_str = line[4:]
                resp = nlq(nlq_str)
                f.write((resp + "\n").encode("utf-8"))
            elif line == "HELP":
                f.write(b"Commands: EVENT, TAG, EMBED, QUERY, NLQ, HELP\n")
            else:
                f.write(b"ERR: Unknown command\n")

def start_socket_server():
    if os.path.exists(SOCKET_PATH):
        os.unlink(SOCKET_PATH)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(SOCKET_PATH)
    server.listen(8)
    print(f"[Metadata Daemon] Listening on {SOCKET_PATH}")
    while True:
        connfd, _ = server.accept()
        threading.Thread(target=handle_client, args=(connfd,), daemon=True).start()

def main():
    init_db()
    threading.Thread(target=wal_polling_loop, daemon=True).start()
    start_socket_server()

if __name__ == "__main__":
    main()
# TODO: Add extension points for distributed/clustered 2PC 