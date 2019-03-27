/*
 * Copyright (c) 2015, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <iostream>

#include "ucl.h"

// C++11 API inspired by json11: https://github.com/dropbox/json11/

namespace ucl {

struct ucl_map_construct_t { };
constexpr ucl_map_construct_t ucl_map_construct = ucl_map_construct_t();
struct ucl_array_construct_t { };
constexpr ucl_array_construct_t ucl_array_construct = ucl_array_construct_t();

class Ucl final {
private:

	struct ucl_deleter {
		void operator() (ucl_object_t *obj) {
			ucl_object_unref (obj);
		}
	};

	static int
	append_char (unsigned char c, size_t nchars, void *ud)
	{
		std::string *out = reinterpret_cast<std::string *>(ud);

		out->append (nchars, (char)c);

		return nchars;
	}
	static int
	append_len (unsigned const char *str, size_t len, void *ud)
	{
		std::string *out = reinterpret_cast<std::string *>(ud);

		out->append ((const char *)str, len);

		return len;
	}
	static int
	append_int (int64_t elt, void *ud)
	{
		std::string *out = reinterpret_cast<std::string *>(ud);
		auto nstr = std::to_string (elt);

		out->append (nstr);

		return nstr.size ();
	}
	static int
	append_double (double elt, void *ud)
	{
		std::string *out = reinterpret_cast<std::string *>(ud);
		auto nstr = std::to_string (elt);

		out->append (nstr);

		return nstr.size ();
	}

	static struct ucl_emitter_functions default_emit_funcs()
	{
		struct ucl_emitter_functions func = {
			Ucl::append_char,
			Ucl::append_len,
			Ucl::append_int,
			Ucl::append_double,
			nullptr,
			nullptr
		};

		return func;
	};

	static bool ucl_variable_getter(const unsigned char *data, size_t len,
			unsigned char ** /*replace*/, size_t * /*replace_len*/, bool *need_free, void* ud)
	{
        *need_free = false;

		auto vars = reinterpret_cast<std::set<std::string> *>(ud);
		if (vars && data && len != 0) {
			vars->emplace (data, data + len);
		}
		return false;
	}

	static bool ucl_variable_replacer (const unsigned char *data, size_t len,
			unsigned char **replace, size_t *replace_len, bool *need_free, void* ud)
	{
		*need_free = false;

		auto replacer = reinterpret_cast<variable_replacer *>(ud);
		if (!replacer) {
			return false;
        }

		std::string var_name (data, data + len);
		if (!replacer->is_variable (var_name)) {
			return false;
        }

		std::string var_value = replacer->replace (var_name);
		if (var_value.empty ()) {
			return false;
        }

		*replace = (unsigned char *)UCL_ALLOC (var_value.size ());
		memcpy (*replace, var_value.data (), var_value.size ());

		*replace_len = var_value.size ();
		*need_free = true;

		return true;
	}

	template <typename C, typename P>
	static Ucl parse_with_strategy_function (C config_func, P parse_func, std::string &err)
	{
		auto parser = ucl_parser_new (UCL_PARSER_DEFAULT);

		config_func (parser);

		if (!parse_func (parser)) {
			err.assign (ucl_parser_get_error (parser));
			ucl_parser_free (parser);

			return nullptr;
		}

		auto obj = ucl_parser_get_object (parser);
		ucl_parser_free (parser);

		// Obj will handle ownership
		return Ucl (obj);
	}

	std::unique_ptr<ucl_object_t, ucl_deleter> obj;

public:
	class const_iterator {
	private:
		struct ucl_iter_deleter {
			void operator() (ucl_object_iter_t it) {
				ucl_object_iterate_free (it);
			}
		};
		std::shared_ptr<void> it;
		std::unique_ptr<Ucl> cur;
	public:
		typedef std::forward_iterator_tag iterator_category;

		const_iterator(const Ucl &obj) {
			it = std::shared_ptr<void>(ucl_object_iterate_new (obj.obj.get()),
				ucl_iter_deleter());
			cur.reset (new Ucl(ucl_object_iterate_safe (it.get(), true)));
			if (cur->type() == UCL_NULL) {
				it.reset ();
				cur.reset ();
			}
		}

		const_iterator() {}
		const_iterator(const const_iterator &other) = delete;
		const_iterator(const_iterator &&other) = default;
		~const_iterator() {}

		const_iterator& operator=(const const_iterator &other) = delete;
		const_iterator& operator=(const_iterator &&other) = default;

		bool operator==(const const_iterator &other) const
		{
			if (cur && other.cur) {
				return cur->obj.get() == other.cur->obj.get();
			}

			return !cur && !other.cur;
		}

		bool operator!=(const const_iterator &other) const
		{
			return !(*this == other);
		}

		const_iterator& operator++()
		{
			if (it) {
				cur.reset (new Ucl(ucl_object_iterate_safe (it.get(), true)));
			}

			if (cur && cur->type() == UCL_NULL) {
				it.reset ();
				cur.reset ();
			}

			return *this;
		}

		const Ucl& operator*() const
		{
			return *cur;
		}
		const Ucl* operator->() const
		{
			return cur.get();
		}
	};

	struct variable_replacer {
		virtual ~variable_replacer() {}

		virtual bool is_variable (const std::string &str) const
		{
			return !str.empty ();
		}

		virtual std::string replace (const std::string &var) const = 0;
	};

	// We grab ownership if get non-const ucl_object_t
	Ucl(ucl_object_t *other) {
		obj.reset (other);
	}

	// Shared ownership
	Ucl(const ucl_object_t *other) {
		obj.reset (ucl_object_ref (other));
	}

	Ucl(const Ucl &other) {
		obj.reset (ucl_object_ref (other.obj.get()));
	}

	Ucl(Ucl &&other) {
		obj.swap (other.obj);
	}

	Ucl() noexcept {
		obj.reset (ucl_object_typed_new (UCL_NULL));
	}
	Ucl(std::nullptr_t) noexcept {
		obj.reset (ucl_object_typed_new (UCL_NULL));
	}
	Ucl(double value) {
		obj.reset (ucl_object_typed_new (UCL_FLOAT));
		obj->value.dv = value;
	}
	Ucl(int64_t value) {
		obj.reset (ucl_object_typed_new (UCL_INT));
		obj->value.iv = value;
	}
	Ucl(bool value) {
		obj.reset (ucl_object_typed_new (UCL_BOOLEAN));
		obj->value.iv = static_cast<int64_t>(value);
	}
	Ucl(const std::string &value) {
		obj.reset (ucl_object_fromstring_common (value.data (), value.size (),
				UCL_STRING_RAW));
	}
	Ucl(const char *value) {
		obj.reset (ucl_object_fromstring_common (value, 0, UCL_STRING_RAW));
	}

	// Implicit constructor: anything with a to_json() function.
	template <class T, class = decltype(&T::to_ucl)>
	Ucl(const T &t) : Ucl(t.to_ucl()) {}

	// Implicit constructor: map-like objects (std::map, std::unordered_map, etc)
	template <class M, typename std::enable_if<
		std::is_constructible<std::string, typename M::key_type>::value
		&& std::is_constructible<Ucl, typename M::mapped_type>::value,
		int>::type = 0>
	Ucl(const M &m) {
		obj.reset (ucl_object_typed_new (UCL_OBJECT));
		auto cobj = obj.get ();

		for (const auto &e : m) {
			ucl_object_insert_key (cobj, ucl_object_ref (e.second.obj.get()),
					e.first.data (), e.first.size (), true);
		}
	}

	// Implicit constructor: vector-like objects (std::list, std::vector, std::set, etc)
	template <class V, typename std::enable_if<
		std::is_constructible<Ucl, typename V::value_type>::value,
		int>::type = 0>
	Ucl(const V &v) {
		obj.reset (ucl_object_typed_new (UCL_ARRAY));
		auto cobj = obj.get ();

		for (const auto &e : v) {
			ucl_array_append (cobj, ucl_object_ref (e.obj.get()));
		}
	}

	ucl_type_t type () const {
		if (obj) {
			return ucl_object_type (obj.get ());
		}
		return UCL_NULL;
	}

	const std::string key () const {
		std::string res;

		if (obj->key) {
			res.assign (obj->key, obj->keylen);
		}

		return res;
	}

	double number_value (const double default_val = 0.0) const
	{
		double res;

		if (ucl_object_todouble_safe(obj.get(), &res)) {
			return res;
		}

		return default_val;
	}

	int64_t int_value (const int64_t default_val = 0) const
	{
		int64_t res;

		if (ucl_object_toint_safe(obj.get(), &res)) {
			return res;
		}

		return default_val;
	}

	bool bool_value (const bool default_val = false) const
	{
		bool res;

		if (ucl_object_toboolean_safe(obj.get(), &res)) {
			return res;
		}

		return default_val;
	}

	const std::string string_value (const std::string& default_val = "") const
	{
		const char* res = nullptr;

		if (ucl_object_tostring_safe(obj.get(), &res)) {
			return res;
		}

		return default_val;
	}

	const Ucl at (size_t i) const
	{
		if (type () == UCL_ARRAY) {
			return Ucl (ucl_array_find_index (obj.get(), i));
		}

		return Ucl (nullptr);
	}

	const Ucl lookup (const std::string &key) const
	{
		if (type () == UCL_OBJECT) {
			return Ucl (ucl_object_lookup_len (obj.get(),
					key.data (), key.size ()));
		}

		return Ucl (nullptr);
	}

	inline const Ucl operator[] (size_t i) const
	{
		return at(i);
	}

	inline const Ucl operator[](const std::string &key) const
	{
		return lookup(key);
	}
	// Serialize.
	void dump (std::string &out, ucl_emitter_t type = UCL_EMIT_JSON) const
	{
		struct ucl_emitter_functions cbdata;

		cbdata = Ucl::default_emit_funcs();
		cbdata.ud = reinterpret_cast<void *>(&out);

		ucl_object_emit_full (obj.get(), type, &cbdata, nullptr);
	}

	std::string dump (ucl_emitter_t type = UCL_EMIT_JSON) const
	{
		std::string out;

		dump (out, type);

		return out;
	}

	static Ucl parse (const std::string &in, std::string &err)
	{
		return parse (in, std::map<std::string, std::string>(), err);
	}

	static Ucl parse (const std::string &in, const std::map<std::string, std::string> &vars, std::string &err)
	{
		auto config_func = [&vars] (ucl_parser *parser) {
			for (const auto & item : vars) {
				ucl_parser_register_variable (parser, item.first.c_str (), item.second.c_str ());
            }
		};

		auto parse_func = [&in] (ucl_parser *parser) {
			return ucl_parser_add_chunk (parser, (unsigned char *)in.data (), in.size ());
		};

		return parse_with_strategy_function (config_func, parse_func, err);
	}

	static Ucl parse (const std::string &in, const variable_replacer &replacer, std::string &err)
	{
		auto config_func = [&replacer] (ucl_parser *parser) {
			ucl_parser_set_variables_handler (parser, ucl_variable_replacer,
				&const_cast<variable_replacer &>(replacer));
		};

		auto parse_func = [&in] (ucl_parser *parser) {
			return ucl_parser_add_chunk (parser, (unsigned char *) in.data (), in.size ());
		};

		return parse_with_strategy_function (config_func, parse_func, err);
	}

	static Ucl parse (const char *in, std::string &err)
	{
		return parse (in, std::map<std::string, std::string>(), err);
	}

	static Ucl parse (const char *in, const std::map<std::string, std::string> &vars, std::string &err)
	{
		if (!in) {
			err = "null input";
			return nullptr;
		}
		return parse (std::string (in), vars, err);
	}

	static Ucl parse (const char *in, const variable_replacer &replacer, std::string &err)
	{
		if (!in) {
			err = "null input";
			return nullptr;
		}
		return parse (std::string(in), replacer, err);
	}

	static Ucl parse_from_file (const std::string &filename, std::string &err)
	{
		return parse_from_file (filename, std::map<std::string, std::string>(), err);
	}

	static Ucl parse_from_file (const std::string &filename, const std::map<std::string, std::string> &vars, std::string &err)
	{
		auto config_func = [&vars] (ucl_parser *parser) {
			for (const auto & item : vars) {
				ucl_parser_register_variable (parser, item.first.c_str (), item.second.c_str ());
            }
		};

		auto parse_func = [&filename] (ucl_parser *parser) {
			return ucl_parser_add_file (parser, filename.c_str ());
		};

		return parse_with_strategy_function (config_func, parse_func, err);
	}

	static Ucl parse_from_file (const std::string &filename, const variable_replacer &replacer, std::string &err)
	{
		auto config_func = [&replacer] (ucl_parser *parser) {
			ucl_parser_set_variables_handler (parser, ucl_variable_replacer,
				&const_cast<variable_replacer &>(replacer));
		};

		auto parse_func = [&filename] (ucl_parser *parser) {
			return ucl_parser_add_file (parser, filename.c_str ());
		};

		return parse_with_strategy_function (config_func, parse_func, err);
	}

	static std::vector<std::string> find_variable (const std::string &in)
	{
		auto parser = ucl_parser_new (UCL_PARSER_DEFAULT);

		std::set<std::string> vars;
		ucl_parser_set_variables_handler (parser, ucl_variable_getter, &vars);
		ucl_parser_add_chunk (parser, (const unsigned char *)in.data (), in.size ());
		ucl_parser_free (parser);

		std::vector<std::string> result;
		std::move (vars.begin (), vars.end (), std::back_inserter (result));
		return result;
	}

	static std::vector<std::string> find_variable (const char *in)
	{
		if (!in) {
			return std::vector<std::string>();
		}
		return find_variable (std::string (in));
	}

	static std::vector<std::string> find_variable_from_file (const std::string &filename)
	{
		auto parser = ucl_parser_new (UCL_PARSER_DEFAULT);

		std::set<std::string> vars;
		ucl_parser_set_variables_handler (parser, ucl_variable_getter, &vars);
		ucl_parser_add_file (parser, filename.c_str ());
		ucl_parser_free (parser);

		std::vector<std::string> result;
		std::move (vars.begin (), vars.end (), std::back_inserter (result));
		return std::move (result);
	}

	Ucl& operator= (Ucl rhs)
	{
		obj.swap (rhs.obj);
		return *this;
	}

	bool operator== (const Ucl &rhs) const
	{
		return ucl_object_compare (obj.get(), rhs.obj.get ()) == 0;
	}
	bool operator< (const Ucl &rhs) const
	{
		return ucl_object_compare (obj.get(), rhs.obj.get ()) < 0;
	}
	bool operator!= (const Ucl &rhs) const { return !(*this == rhs); }
	bool operator<= (const Ucl &rhs) const { return !(rhs < *this); }
	bool operator> (const Ucl &rhs) const { return (rhs < *this); }
	bool operator>= (const Ucl &rhs) const { return !(*this < rhs); }

	explicit operator bool () const
	{
		if (!obj || type() == UCL_NULL) {
			return false;
		}

		if (type () == UCL_BOOLEAN) {
			return bool_value ();
		}

		return true;
	}

	const_iterator begin() const
	{
		return const_iterator(*this);
	}
	const_iterator cbegin() const
	{
		return const_iterator(*this);
	}
	const_iterator end() const
	{
		return const_iterator();
	}
	const_iterator cend() const
	{
		return const_iterator();
	}
};

};
