# -*- mode: python -*-
# python module to manage Ubuntu kernel .config and annotations
# Copyright © 2022 Canonical Ltd.

import json
import re
import shutil
import tempfile

from abc import abstractmethod
from ast import literal_eval
from os.path import dirname, abspath

from kconfig.version import ANNOTATIONS_FORMAT_VERSION


class Config:
    def __init__(self, fname, do_include=True):
        """
        Basic configuration file object
        """
        self.fname = fname
        self.config = {}
        self.do_include = do_include

        raw_data = self._load(fname)
        self._parse(raw_data)

    @staticmethod
    def _load(fname: str) -> str:
        with open(fname, "rt", encoding="utf-8") as fd:
            data = fd.read()
        return data.rstrip()

    def __str__(self):
        """Return a JSON representation of the config"""
        return json.dumps(self.config, indent=4)

    @abstractmethod
    def _parse(self, data: str):
        pass


class KConfig(Config):
    """
    Parse a .config file, individual config options can be accessed via
    .config[<CONFIG_OPTION>]
    """

    def _parse(self, data: str):
        self.config = {}
        for line in data.splitlines():
            m = re.match(r"^# (CONFIG_.*) is not set$", line)
            if m:
                self.config[m.group(1)] = literal_eval("'n'")
                continue
            m = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line)
            if m:
                self.config[m.group(1)] = literal_eval("'" + m.group(2) + "'")
                continue


class Annotation(Config):
    """
    Parse body of annotations file
    """

    def __init__(self, fname, do_include=True, do_json=False):
        self.do_json = do_json
        super().__init__(fname, do_include=True)

    def _parse_body(self, data: str, parent=True):
        for line in data.splitlines():
            # Replace tabs with spaces, squeeze multiple into singles and
            # remove leading and trailing spaces
            line = line.replace("\t", " ")
            line = re.sub(r" +", " ", line)
            line = line.strip()

            # Ignore empty lines
            if not line:
                continue

            # Catpure flavors of included files
            if line.startswith("# FLAVOUR: "):
                self.include_flavour += line.split(" ")[2:]
                continue

            # Ignore comments
            if line.startswith("#"):
                continue

            # Handle includes (recursively)
            m = re.match(r'^include\s+"?([^"]*)"?', line)
            if m:
                if parent:
                    self.include.append(m.group(1))
                if self.do_include:
                    include_fname = dirname(abspath(self.fname)) + "/" + m.group(1)
                    include_data = self._load(include_fname)
                    self._parse_body(include_data, parent=False)
                continue

            # Handle policy and note lines
            if re.match(r".* (policy|note)<", line):
                try:
                    conf = line.split(" ")[0]
                    if conf in self.config:
                        entry = self.config[conf]
                    else:
                        entry = {"policy": {}}

                    match = False
                    m = re.match(r".* policy<(.*?)>", line)
                    if m:
                        match = True
                        # Update the previous entry considering potential overrides:
                        #  - if the new entry is adding a rule for a new
                        #    arch/flavour, simply add that
                        #  - if the new entry is overriding a previous
                        #    arch-flavour item, then overwrite that item
                        #  - if the new entry is overriding a whole arch, then
                        #    remove all the previous flavour rules of that arch
                        new_entry = literal_eval(m.group(1))
                        for key in new_entry:
                            if key in self.arch:
                                for flavour_key in list(entry["policy"].keys()):
                                    if flavour_key.startswith(key):
                                        del entry["policy"][flavour_key]
                                entry["policy"][key] = new_entry[key]
                            else:
                                entry["policy"][key] = new_entry[key]

                    m = re.match(r".* note<(.*?)>", line)
                    if m:
                        entry["oneline"] = match
                        match = True
                        entry["note"] = "'" + m.group(1).replace("'", "") + "'"

                    if not match:
                        raise SyntaxError("syntax error")
                    self.config[conf] = entry
                except Exception as e:
                    raise SyntaxError(str(e) + f", line = {line}") from e
                continue

            # Invalid line
            raise SyntaxError(f"invalid line: {line}")

    def _legacy_parse(self, data: str):
        """
        Parse main annotations file, individual config options can be accessed
        via self.config[<CONFIG_OPTION>]
        """
        self.config = {}
        self.arch = []
        self.flavour = []
        self.flavour_dep = {}
        self.include = []
        self.header = ""
        self.include_flavour = []

        # Parse header (only main header will considered, headers in includes
        # will be treated as comments)
        for line in data.splitlines():
            if re.match(r"^#.*", line):
                m = re.match(r"^# ARCH: (.*)", line)
                if m:
                    self.arch = list(m.group(1).split(" "))
                m = re.match(r"^# FLAVOUR: (.*)", line)
                if m:
                    self.flavour = list(m.group(1).split(" "))
                m = re.match(r"^# FLAVOUR_DEP: (.*)", line)
                if m:
                    self.flavour_dep = literal_eval(m.group(1))
                self.header += line + "\n"
            else:
                break

        # Return an error if architectures are not defined
        if not self.arch:
            raise SyntaxError("ARCH not defined in annotations")
        # Return an error if flavours are not defined
        if not self.flavour:
            raise SyntaxError("FLAVOUR not defined in annotations")

        # Parse body
        self._parse_body(data)

        # Sanity check: Verify that all FLAVOUR_DEP flavors are valid
        if self.do_include:
            for src, tgt in self.flavour_dep.items():
                if src not in self.flavour:
                    raise SyntaxError(f"Invalid source flavour in FLAVOUR_DEP: {src}")
                if tgt not in self.include_flavour:
                    raise SyntaxError(f"Invalid target flavour in FLAVOUR_DEP: {tgt}")

    def _json_parse(self, data, is_included=False):
        data = json.loads(data)

        # Check if version is supported
        version = data["attributes"]["_version"]
        if version > ANNOTATIONS_FORMAT_VERSION:
            raise SyntaxError(f"annotations format version {version} not supported")

        # Check for top-level annotations vs imported annotations
        if not is_included:
            self.config = data["config"]
            self.arch = data["attributes"]["arch"]
            self.flavour = data["attributes"]["flavour"]
            self.flavour_dep = data["attributes"]["flavour_dep"]
            self.include = data["attributes"]["include"]
            self.include_flavour = []
        else:
            # We are procesing an imported annotations, so merge all the
            # configs and attributes.
            try:
                self.config = data["config"] | self.config
            except TypeError:
                self.config = {**self.config, **data["config"]}
            self.arch = list(set(self.arch) | set(data["attributes"]["arch"]))
            self.flavour = list(set(self.flavour) | set(data["attributes"]["flavour"]))
            self.include_flavour = list(set(self.include_flavour) | set(data["attributes"]["flavour"]))
            self.flavour_dep = self.flavour_dep | data["attributes"]["flavour_dep"]

        # Handle recursive inclusions
        if self.do_include:
            for f in data["attributes"]["include"]:
                include_fname = dirname(abspath(self.fname)) + "/" + f
                data = self._load(include_fname)
                self._json_parse(data, is_included=True)

    def _parse(self, data: str):
        if self.do_json:
            self._json_parse(data, is_included=False)
        else:
            self._legacy_parse(data)

    def _remove_entry(self, config: str):
        if self.config[config]:
            del self.config[config]

    def remove(self, config: str, arch: str = None, flavour: str = None):
        if config not in self.config:
            return
        if arch is not None:
            if flavour is not None:
                flavour = f"{arch}-{flavour}"
            else:
                flavour = arch
            del self.config[config]["policy"][flavour]
            if not self.config[config]["policy"]:
                self._remove_entry(config)
        else:
            self._remove_entry(config)

    def set(
        self,
        config: str,
        arch: str = None,
        flavour: str = None,
        value: str = None,
        note: str = None,
    ):
        if value is not None:
            if config not in self.config:
                self.config[config] = {"policy": {}}
            if arch is not None:
                if flavour is not None:
                    flavour = f"{arch}-{flavour}"
                else:
                    flavour = arch
                self.config[config]["policy"][flavour] = value
            else:
                for a in self.arch:
                    self.config[config]["policy"][a] = value
        if note is not None:
            self.config[config]["note"] = "'" + note.replace("'", "") + "'"

    def update(self, c: KConfig, arch: str, flavour: str = None, configs: list = None):
        """Merge configs from a Kconfig object into Annotation object"""

        # Determine if we need to import all configs or a single config
        if not configs:
            configs = c.config.keys()
            try:
                configs |= self.search_config(arch=arch, flavour=flavour).keys()
            except TypeError:
                configs = {
                    **configs,
                    **self.search_config(arch=arch, flavour=flavour).keys(),
                }

        # Import configs from the Kconfig object into Annotations
        flavour_arg = flavour
        if flavour is not None:
            flavour = arch + f"-{flavour}"
        else:
            flavour = arch
        for conf in configs:
            if conf in c.config:
                val = c.config[conf]
            else:
                val = "-"
            if conf in self.config:
                if "policy" in self.config[conf]:
                    # Add a TODO if a config with a note is changing and print
                    # a warning
                    old_val = self.search_config(config=conf, arch=arch, flavour=flavour_arg)
                    if old_val:
                        old_val = old_val[conf]
                    if val != old_val and "note" in self.config[conf]:
                        self.config[conf]["note"] = "TODO: update note"
                        print(f"WARNING: {conf} changed from {old_val} to {val}, updating note")
                    self.config[conf]["policy"][flavour] = val
                else:
                    self.config[conf]["policy"] = {flavour: val}
            else:
                self.config[conf] = {"policy": {flavour: val}}

    def _compact(self):
        # Try to remove redundant settings: if the config value of a flavour is
        # the same as the one of the main arch simply drop it.
        for conf in self.config.copy():
            if "policy" not in self.config[conf]:
                continue
            for flavour in self.flavour:
                if flavour not in self.config[conf]["policy"]:
                    continue
                m = re.match(r"^(.*?)-(.*)$", flavour)
                if not m:
                    continue
                arch = m.group(1)
                if arch in self.config[conf]["policy"]:
                    if self.config[conf]["policy"][flavour] == self.config[conf]["policy"][arch]:
                        del self.config[conf]["policy"][flavour]
                        continue
                if flavour not in self.flavour_dep:
                    continue
                generic = self.flavour_dep[flavour]
                if generic in self.config[conf]["policy"]:
                    if self.config[conf]["policy"][flavour] == self.config[conf]["policy"][generic]:
                        del self.config[conf]["policy"][flavour]
                        continue
            # Remove rules for flavours / arches that are not supported (not
            # listed in the annotations header).
            for flavour in self.config[conf]["policy"].copy():
                if flavour not in list(set(self.arch + self.flavour)):
                    del self.config[conf]["policy"][flavour]
            # Remove configs that are all undefined across all arches/flavours
            # (unless we have includes)
            if not self.include:
                if "policy" in self.config[conf]:
                    if list(set(self.config[conf]["policy"].values())) == ["-"]:
                        self.config[conf]["policy"] = {}
            # Drop empty rules
            if not self.config[conf]["policy"]:
                del self.config[conf]
            else:
                # Compact same value across all flavour within the same arch
                for arch in self.arch:
                    arch_flavours = [i for i in self.flavour if i.startswith(arch)]
                    value = None
                    for flavour in arch_flavours:
                        if flavour not in self.config[conf]["policy"]:
                            break
                        if value is None:
                            value = self.config[conf]["policy"][flavour]
                        elif value != self.config[conf]["policy"][flavour]:
                            break
                    else:
                        for flavour in arch_flavours:
                            del self.config[conf]["policy"][flavour]
                        self.config[conf]["policy"][arch] = value
        # After the first round of compaction we may end up having configs that
        # are undefined across all arches, so do another round of compaction to
        # drop these settings that are not needed anymore
        # (unless we have includes).
        if not self.include:
            for conf in self.config.copy():
                # Remove configs that are all undefined across all arches/flavours
                if "policy" in self.config[conf]:
                    if list(set(self.config[conf]["policy"].values())) == ["-"]:
                        self.config[conf]["policy"] = {}
                # Drop empty rules
                if not self.config[conf]["policy"]:
                    del self.config[conf]

    @staticmethod
    def _sorted(config):
        """Sort configs alphabetically but return configs with a note first"""
        w_note = []
        wo_note = []
        for c in sorted(config):
            if "note" in config[c]:
                w_note.append(c)
            else:
                wo_note.append(c)
        return w_note + wo_note

    def save(self, fname: str):
        """Save annotations data to the annotation file"""
        # Compact annotations structure
        self._compact()

        # Save annotations to disk
        with tempfile.NamedTemporaryFile(mode="w+t", delete=False) as tmp:
            # Write header
            tmp.write(self.header + "\n")

            # Write includes
            for i in self.include:
                tmp.write(f'include "{i}"\n')
            if self.include:
                tmp.write("\n")

            # Write config annotations and notes
            tmp.flush()
            shutil.copy(tmp.name, fname)
            tmp_a = Annotation(fname)

            # Only save local differences (preserve includes)
            marker = False
            for conf in self._sorted(self.config):
                new_val = self.config[conf]
                if "policy" not in new_val:
                    continue

                # If new_val is a subset of old_val, skip it unless there are
                # new notes that are different than the old ones.
                old_val = tmp_a.config.get(conf)
                if old_val and "policy" in old_val:
                    try:
                        can_skip = old_val["policy"] == old_val["policy"] | new_val["policy"]
                    except TypeError:
                        can_skip = old_val["policy"] == {
                            **old_val["policy"],
                            **new_val["policy"],
                        }
                    if can_skip:
                        if "note" not in new_val:
                            continue
                        if "note" in old_val and "note" in new_val:
                            if old_val["note"] == new_val["note"]:
                                continue

                # Write out the policy (and note) line(s)
                val = dict(sorted(new_val["policy"].items()))
                line = f"{conf : <47} policy<{val}>"
                if "note" in new_val:
                    val = new_val["note"]
                    if new_val.get("oneline", False):
                        # Single line
                        line += f" note<{val}>"
                    else:
                        # Separate policy and note lines,
                        # followed by an empty line
                        line += f"\n{conf : <47} note<{val}>\n"
                elif not marker:
                    # Write out a marker indicating the start of annotations
                    # without notes
                    tmp.write("\n# ---- Annotations without notes ----\n\n")
                    marker = True
                tmp.write(line + "\n")

            # Replace annotations with the updated version
            tmp.flush()
            shutil.move(tmp.name, fname)

    def search_config(self, config: str = None, arch: str = None, flavour: str = None) -> dict:
        """Return config value of a specific config option or architecture"""
        if flavour is None:
            flavour = "generic"
        flavour = f"{arch}-{flavour}"
        if flavour in self.flavour_dep:
            generic = self.flavour_dep[flavour]
        else:
            generic = flavour
        if config is None and arch is None:
            # Get all config options for all architectures
            return self.config
        if config is None and arch is not None:
            # Get config options of a specific architecture
            ret = {}
            for c, val in self.config.items():
                if "policy" not in val:
                    continue
                if flavour in val["policy"]:
                    ret[c] = val["policy"][flavour]
                elif generic != flavour and generic in val["policy"]:
                    ret[c] = val["policy"][generic]
                elif arch in val["policy"]:
                    ret[c] = val["policy"][arch]
            return ret
        if config is not None and arch is None:
            # Get a specific config option for all architectures
            return self.config[config] if config in self.config else None
        if config is not None and arch is not None:
            # Get a specific config option for a specific architecture
            if config in self.config:
                if "policy" in self.config[config]:
                    if flavour in self.config[config]["policy"]:
                        return {config: self.config[config]["policy"][flavour]}
                    if generic != flavour and generic in self.config[config]["policy"]:
                        return {config: self.config[config]["policy"][generic]}
                    if arch in self.config[config]["policy"]:
                        return {config: self.config[config]["policy"][arch]}
        return None

    @staticmethod
    def to_config(data: dict) -> str:
        """Convert annotations data to .config format"""
        s = ""
        for c in data:
            v = data[c]
            if v == "n":
                s += f"# {c} is not set\n"
            elif v == "-":
                pass
            else:
                s += f"{c}={v}\n"
        return s.rstrip()
